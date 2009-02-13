/*
 * Copyright (C) 2008  Barracuda Networks, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>
#include <QVariant>
#include <QFileDialog>
#include <QDir>
#include <QHostAddress>
#include <QUdpSocket>
#include <QtPlugin>
#include <QLibrary>
#include "psimedia.h"
#include "ui_mainwin.h"
#include "ui_config.h"

#define BASE_PORT_MIN 1
#define BASE_PORT_MAX 65534

static QString urlishEncode(const QString &in)
{
	QString out;
	for(int n = 0; n < in.length(); ++n)
	{
		if(in[n] == '%' || in[n] == ',' ||  in[n] == ';' || in[n] == ':' || in[n] == '\n')
		{
			unsigned char c = (unsigned char)in[n].toLatin1();
			out += QString().sprintf("%%%02x", c);
		}
		else
			out += in[n];
	}
	return out;
}

static QString urlishDecode(const QString &in)
{
	QString out;
	for(int n = 0; n < in.length(); ++n)
	{
		if(in[n] == '%')
		{
			if(n + 2 >= in.length())
				return QString();

			QString hex = in.mid(n + 1, 2);
			bool ok;
			int x = hex.toInt(&ok, 16);
			if(!ok)
				return QString();

			unsigned char c = (unsigned char)x;
			out += c;
			n += 2;
		}
		else
			out += in[n];
	}
	return out;
}

static QString payloadInfoToString(const PsiMedia::PayloadInfo &info)
{
	QStringList list;
	list += QString::number(info.id());
	list += info.name();
	list += QString::number(info.clockrate());
	list += QString::number(info.channels());
	list += QString::number(info.ptime());
	list += QString::number(info.maxptime());
	foreach(const PsiMedia::PayloadInfo::Parameter &p, info.parameters())
		list += p.name + '=' + p.value;

	for(int n = 0; n < list.count(); ++n)
		list[n] = urlishEncode(list[n]);
	return list.join(",");
}

static PsiMedia::PayloadInfo stringToPayloadInfo(const QString &in)
{
	QStringList list = in.split(',');
	if(list.count() < 6)
		return PsiMedia::PayloadInfo();

	for(int n = 0; n < list.count(); ++n)
	{
		QString str = urlishDecode(list[n]);
		if(str.isEmpty())
			return PsiMedia::PayloadInfo();
		list[n] = str;
	}

	PsiMedia::PayloadInfo out;
	bool ok;
	int x;

	x = list[0].toInt(&ok);
	if(!ok)
		return PsiMedia::PayloadInfo();
	out.setId(x);

	out.setName(list[1]);

	x = list[2].toInt(&ok);
	if(!ok)
		return PsiMedia::PayloadInfo();
	out.setClockrate(x);

	x = list[3].toInt(&ok);
	if(!ok)
		return PsiMedia::PayloadInfo();
	out.setChannels(x);

	x = list[4].toInt(&ok);
	if(!ok)
		return PsiMedia::PayloadInfo();
	out.setPtime(x);

	x = list[5].toInt(&ok);
	if(!ok)
		return PsiMedia::PayloadInfo();
	out.setMaxptime(x);

	QList<PsiMedia::PayloadInfo::Parameter> plist;
	for(int n = 6; n < list.count(); ++n)
	{
		x = list[n].indexOf('=');
		if(x == -1)
			return PsiMedia::PayloadInfo();
		PsiMedia::PayloadInfo::Parameter p;
		p.name = list[n].mid(0, x);
		p.value = list[n].mid(x + 1);
		plist += p;
	}
	out.setParameters(plist);

	return out;
}

static QString payloadInfoToCodecString(const PsiMedia::PayloadInfo *audio, const PsiMedia::PayloadInfo *video)
{
	QStringList list;
	if(audio)
		list += QString("A:") + payloadInfoToString(*audio);
	if(video)
		list += QString("V:") + payloadInfoToString(*video);
	return list.join(";");
}

static bool codecStringToPayloadInfo(const QString &in, PsiMedia::PayloadInfo *audio, PsiMedia::PayloadInfo *video)
{
	QStringList list = in.split(';');
	foreach(const QString &s, list)
	{
		int x = s.indexOf(':');
		if(x == -1)
			return false;

		QString var = s.mid(0, x);
		QString val = s.mid(x + 1);
		if(val.isEmpty())
			return false;

		PsiMedia::PayloadInfo info = stringToPayloadInfo(val);
		if(info.isNull())
			return false;

		if(var == "A" && audio)
			*audio = info;
		else if(var == "V" && video)
			*video = info;
	}

	return true;
}

Q_DECLARE_METATYPE(PsiMedia::AudioParams);
Q_DECLARE_METATYPE(PsiMedia::VideoParams);

class Configuration
{
public:
	bool liveInput;
	QString audioOutDeviceId, audioInDeviceId, videoInDeviceId;
	QString file;
	bool loopFile;
	PsiMedia::AudioParams audioParams;
	PsiMedia::VideoParams videoParams;

	Configuration() :
		liveInput(false),
		loopFile(false)
	{
	}
};

class PsiMediaFeaturesSnapshot
{
public:
	QList<PsiMedia::Device> audioOutputDevices;
	QList<PsiMedia::Device> audioInputDevices;
	QList<PsiMedia::Device> videoInputDevices;
	QList<PsiMedia::AudioParams> supportedAudioModes;
	QList<PsiMedia::VideoParams> supportedVideoModes;

	PsiMediaFeaturesSnapshot()
	{
		PsiMedia::Features f;
		f.lookup();
		f.waitForFinished();

		audioOutputDevices = f.audioOutputDevices();
		audioInputDevices = f.audioInputDevices();
		videoInputDevices = f.videoInputDevices();
		supportedAudioModes = f.supportedAudioModes();
		supportedVideoModes = f.supportedVideoModes();
	}
};

// get default settings
static Configuration getDefaultConfiguration()
{
	Configuration config;
	config.liveInput = true;
	config.loopFile = true;

	PsiMediaFeaturesSnapshot snap;

	QList<PsiMedia::Device> devs;

	devs = snap.audioOutputDevices;
	if(!devs.isEmpty())
		config.audioOutDeviceId = devs.first().id();

	devs = snap.audioInputDevices;
	if(!devs.isEmpty())
		config.audioInDeviceId = devs.first().id();

	devs = snap.videoInputDevices;
	if(!devs.isEmpty())
		config.videoInDeviceId = devs.first().id();

	config.audioParams = snap.supportedAudioModes.first();
	config.videoParams = snap.supportedVideoModes.first();

	return config;
}

// adjust any invalid settings to nearby valid ones
static Configuration adjustConfiguration(const Configuration &in, const PsiMediaFeaturesSnapshot &snap)
{
	Configuration out = in;
	bool found;

	if(!out.audioOutDeviceId.isEmpty())
	{
		found = false;
		foreach(const PsiMedia::Device &dev, snap.audioOutputDevices)
		{
			if(out.audioOutDeviceId == dev.id())
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			if(!snap.audioOutputDevices.isEmpty())
				out.audioOutDeviceId = snap.audioOutputDevices.first().id();
			else
				out.audioOutDeviceId.clear();
		}
	}

	if(!out.audioInDeviceId.isEmpty())
	{
		found = false;
		foreach(const PsiMedia::Device &dev, snap.audioInputDevices)
		{
			if(out.audioInDeviceId == dev.id())
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			if(!snap.audioInputDevices.isEmpty())
				out.audioInDeviceId = snap.audioInputDevices.first().id();
			else
				out.audioInDeviceId.clear();
		}
	}

	if(!out.videoInDeviceId.isEmpty())
	{
		found = false;
		foreach(const PsiMedia::Device &dev, snap.videoInputDevices)
		{
			if(out.videoInDeviceId == dev.id())
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			if(!snap.videoInputDevices.isEmpty())
				out.videoInDeviceId = snap.videoInputDevices.first().id();
			else
				out.videoInDeviceId.clear();
		}
	}

	found = false;
	foreach(const PsiMedia::AudioParams &params, snap.supportedAudioModes)
	{
		if(out.audioParams == params)
		{
			found = true;
			break;
		}
	}
	if(!found)
		out.audioParams = snap.supportedAudioModes.first();

	found = false;
	foreach(const PsiMedia::VideoParams &params, snap.supportedVideoModes)
	{
		if(out.videoParams == params)
		{
			found = true;
			break;
		}
	}
	if(!found)
		out.videoParams = snap.supportedVideoModes.first();

	return out;
}

class ConfigDlg : public QDialog
{
	Q_OBJECT

public:
	Ui::Config ui;
	Configuration config;

	ConfigDlg(const Configuration &_config, QWidget *parent = 0) :
		QDialog(parent),
		config(_config)
	{
		ui.setupUi(this);
		setWindowTitle(tr("Configure Audio/Video"));

		ui.lb_audioInDevice->setEnabled(false);
		ui.cb_audioInDevice->setEnabled(false);
		ui.lb_videoInDevice->setEnabled(false);
		ui.cb_videoInDevice->setEnabled(false);
		ui.lb_file->setEnabled(false);
		ui.le_file->setEnabled(false);
		ui.tb_file->setEnabled(false);
		ui.ck_loop->setEnabled(false);

		connect(ui.rb_sendLive, SIGNAL(toggled(bool)), SLOT(live_toggled(bool)));
		connect(ui.rb_sendFile, SIGNAL(toggled(bool)), SLOT(file_toggled(bool)));
		connect(ui.tb_file, SIGNAL(clicked()), SLOT(file_choose()));

		PsiMediaFeaturesSnapshot snap;

		ui.cb_audioOutDevice->addItem("<None>", QString());
		foreach(const PsiMedia::Device &dev, snap.audioOutputDevices)
			ui.cb_audioOutDevice->addItem(dev.name(), dev.id());

		ui.cb_audioInDevice->addItem("<None>", QString());
		foreach(const PsiMedia::Device &dev, snap.audioInputDevices)
			ui.cb_audioInDevice->addItem(dev.name(), dev.id());

		ui.cb_videoInDevice->addItem("<None>", QString());
		foreach(const PsiMedia::Device &dev, snap.videoInputDevices)
			ui.cb_videoInDevice->addItem(dev.name(), dev.id());

		foreach(const PsiMedia::AudioParams &params, snap.supportedAudioModes)
		{
			QString codec = params.codec();
			if(codec == "vorbis" || codec == "speex")
				codec[0] = codec[0].toUpper();
			else
				codec = codec.toUpper();
			QString hz = QString::number(params.sampleRate() / 1000);
			QString chanstr;
			if(params.channels() == 1)
				chanstr = "Mono";
			else if(params.channels() == 2)
				chanstr = "Stereo";
			else
				chanstr = QString("Channels: %1").arg(params.channels());
			QString str = QString("%1, %2KHz, %3-bit, %4").arg(codec).arg(hz).arg(params.sampleSize()).arg(chanstr);

			ui.cb_audioMode->addItem(str, qVariantFromValue<PsiMedia::AudioParams>(params));
		}

		foreach(const PsiMedia::VideoParams &params, snap.supportedVideoModes)
		{
			QString codec = params.codec();
			if(codec == "theora")
				codec[0] = codec[0].toUpper();
			else
				codec = codec.toUpper();
			QString sizestr = QString("%1x%2").arg(params.size().width()).arg(params.size().height());
			QString str = QString("%1, %2 @ %3fps").arg(codec).arg(sizestr).arg(params.fps());

			ui.cb_videoMode->addItem(str, qVariantFromValue<PsiMedia::VideoParams>(params));
		}

		// the following lookups are guaranteed, since the config is
		//   adjusted to all valid values as necessary
		config = adjustConfiguration(config, snap);
		ui.cb_audioOutDevice->setCurrentIndex(ui.cb_audioOutDevice->findData(config.audioOutDeviceId));
		ui.cb_audioInDevice->setCurrentIndex(ui.cb_audioInDevice->findData(config.audioInDeviceId));
		ui.cb_videoInDevice->setCurrentIndex(ui.cb_videoInDevice->findData(config.videoInDeviceId));
		ui.cb_audioMode->setCurrentIndex(findAudioParamsData(ui.cb_audioMode, config.audioParams));
		ui.cb_videoMode->setCurrentIndex(findVideoParamsData(ui.cb_videoMode, config.videoParams));
		if(config.liveInput)
			ui.rb_sendLive->setChecked(true);
		else
			ui.rb_sendFile->setChecked(true);
		ui.le_file->setText(config.file);
		ui.ck_loop->setChecked(config.loopFile);
	}

	// apparently custom QVariants can't be compared, so we have to
	//   make our own find functions for the comboboxes
	int findAudioParamsData(QComboBox *cb, const PsiMedia::AudioParams &params)
	{
		for(int n = 0; n < cb->count(); ++n)
		{
			if(qVariantValue<PsiMedia::AudioParams>(cb->itemData(n)) == params)
				return n;
		}

		return -1;
	}

	int findVideoParamsData(QComboBox *cb, const PsiMedia::VideoParams &params)
	{
		for(int n = 0; n < cb->count(); ++n)
		{
			if(qVariantValue<PsiMedia::VideoParams>(cb->itemData(n)) == params)
				return n;
		}

		return -1;
	}

protected:
	virtual void accept()
	{
		config.audioOutDeviceId = ui.cb_audioOutDevice->itemData(ui.cb_audioOutDevice->currentIndex()).toString();
		config.audioInDeviceId = ui.cb_audioInDevice->itemData(ui.cb_audioInDevice->currentIndex()).toString();
		config.audioParams = qVariantValue<PsiMedia::AudioParams>(ui.cb_audioMode->itemData(ui.cb_audioMode->currentIndex()));
		config.videoInDeviceId = ui.cb_videoInDevice->itemData(ui.cb_videoInDevice->currentIndex()).toString();
		config.videoParams = qVariantValue<PsiMedia::VideoParams>(ui.cb_videoMode->itemData(ui.cb_videoMode->currentIndex()));
		config.liveInput = ui.rb_sendLive->isChecked();
		config.file = ui.le_file->text();
		config.loopFile = ui.ck_loop->isChecked();

		QDialog::accept();
	}

private slots:
	void live_toggled(bool on)
	{
		ui.lb_audioInDevice->setEnabled(on);
		ui.cb_audioInDevice->setEnabled(on);
		ui.lb_videoInDevice->setEnabled(on);
		ui.cb_videoInDevice->setEnabled(on);
	}

	void file_toggled(bool on)
	{
		ui.lb_file->setEnabled(on);
		ui.le_file->setEnabled(on);
		ui.tb_file->setEnabled(on);
		ui.ck_loop->setEnabled(on);
	}

	void file_choose()
	{
		QString fileName = QFileDialog::getOpenFileName(this,
			tr("Open File"),
			QCoreApplication::applicationDirPath(),
			tr("Ogg Audio/Video (*.oga *.ogv *.ogg)"));
		if(!fileName.isEmpty())
			ui.le_file->setText(fileName);
	}
};

// handles two udp sockets
class RtpSocketGroup : public QObject
{
	Q_OBJECT

public:
	QUdpSocket socket[2];

	RtpSocketGroup(QObject *parent = 0) :
		QObject(parent)
	{
		connect(&socket[0], SIGNAL(readyRead()), SLOT(sock_readyRead()));
		connect(&socket[1], SIGNAL(readyRead()), SLOT(sock_readyRead()));
		connect(&socket[0], SIGNAL(bytesWritten(qint64)), SLOT(sock_bytesWritten(qint64)));
		connect(&socket[1], SIGNAL(bytesWritten(qint64)), SLOT(sock_bytesWritten(qint64)));
	}

	bool bind(int basePort)
	{
		if(!socket[0].bind(basePort))
			return false;
		if(!socket[1].bind(basePort + 1))
			return false;
		return true;
	}

signals:
	void readyRead(int offset);
	void datagramWritten(int offset);

private slots:
	void sock_readyRead()
	{
		QUdpSocket *udp = (QUdpSocket *)sender();
		if(udp == &socket[0])
			emit readyRead(0);
		else
			emit readyRead(1);
	}

	void sock_bytesWritten(qint64 bytes)
	{
		Q_UNUSED(bytes);

		QUdpSocket *udp = (QUdpSocket *)sender();
		if(udp == &socket[0])
			emit datagramWritten(0);
		else
			emit datagramWritten(1);
	}
};

// bind a channel to a socket group.
// takes ownership of socket group.
class RtpBinding : public QObject
{
	Q_OBJECT

public:
	enum Mode
	{
		Send,
		Receive
	};

	Mode mode;
	PsiMedia::RtpChannel *channel;
	RtpSocketGroup *socketGroup;
	QHostAddress sendAddress;
	int sendBasePort;

	RtpBinding(Mode _mode, PsiMedia::RtpChannel *_channel, RtpSocketGroup *_socketGroup, QObject *parent = 0) :
		QObject(parent),
		mode(_mode),
		channel(_channel),
		socketGroup(_socketGroup),
		sendBasePort(-1)
	{
		socketGroup->setParent(this);
		connect(socketGroup, SIGNAL(readyRead(int)), SLOT(net_ready(int)));
		connect(socketGroup, SIGNAL(datagramWritten(int)), SLOT(net_written(int)));
		connect(channel, SIGNAL(readyRead()), SLOT(app_ready()));
		connect(channel, SIGNAL(packetsWritten(int)), SLOT(app_written(int)));
	}

private slots:
	void net_ready(int offset)
	{
		// here we handle packets received from the network, that
		//   we need to give to psimedia

		while(socketGroup->socket[offset].hasPendingDatagrams())
		{
			int size = (int)socketGroup->socket[offset].pendingDatagramSize();
			QByteArray rawValue(size, offset);
			QHostAddress fromAddr;
			quint16 fromPort;
			if(socketGroup->socket[offset].readDatagram(rawValue.data(), size, &fromAddr, &fromPort) == -1)
				continue;

			// if we are sending RTP, we should not be receiving
			//   anything on offset 0
			if(mode == Send && offset == 0)
				continue;

			PsiMedia::RtpPacket packet(rawValue, offset);
			channel->write(packet);
		}
	}

	void net_written(int offset)
	{
		Q_UNUSED(offset);
		// do nothing
	}

	void app_ready()
	{
		// here we handle packets that psimedia wants to send out,
		//   that we need to give to the network

		while(channel->packetsAvailable() > 0)
		{
			PsiMedia::RtpPacket packet = channel->read();
			int offset = packet.portOffset();
			if(offset < 0 || offset > 1)
				continue;

			// if we are receiving RTP, we should not be sending
			//   anything on offset 0
			if(mode == Receive && offset == 0)
				continue;

			if(sendAddress.isNull() || sendBasePort < BASE_PORT_MIN || sendBasePort > BASE_PORT_MAX)
				continue;

			socketGroup->socket[offset].writeDatagram(packet.rawValue(), sendAddress, sendBasePort + offset);
		}
	}

	void app_written(int count)
	{
		Q_UNUSED(count);
		// do nothing
	}
};

class MainWin : public QMainWindow
{
	Q_OBJECT

public:
	Ui::MainWin ui;
	QAction *action_AboutProvider;
	QString creditName;
	PsiMedia::RtpSession producer;
	PsiMedia::RtpSession receiver;
	Configuration config;
	bool transmitAudio, transmitVideo, transmitting;
	bool receiveAudio, receiveVideo;
	RtpBinding *sendAudioRtp, *sendVideoRtp;
	RtpBinding *receiveAudioRtp, *receiveVideoRtp;
	bool recording;
	QFile *recordFile;

	MainWin() :
		action_AboutProvider(0),
		producer(this),
		receiver(this),
		sendAudioRtp(0),
		sendVideoRtp(0),
		receiveAudioRtp(0),
		receiveVideoRtp(0),
		recording(false),
		recordFile(0)
	{
		ui.setupUi(this);
		setWindowTitle(tr("PsiMedia Test"));

		creditName = PsiMedia::creditName();
		if(!creditName.isEmpty())
		{
			action_AboutProvider = new QAction(this);
			action_AboutProvider->setText(tr("About %1").arg(creditName));
			ui.menu_Help->addAction(action_AboutProvider);
			connect(action_AboutProvider, SIGNAL(triggered()), SLOT(doAboutProvider()));
		}

		config = getDefaultConfiguration();

		ui.pb_transmit->setEnabled(false);
		ui.pb_stopSend->setEnabled(false);
		ui.pb_stopReceive->setEnabled(false);
		ui.pb_record->setEnabled(false);
		ui.le_sendConfig->setReadOnly(true);
		ui.lb_sendConfig->setEnabled(false);
		ui.le_sendConfig->setEnabled(false);
		ui.sl_mic->setMinimum(0);
		ui.sl_mic->setMaximum(100);
		ui.sl_spk->setMinimum(0);
		ui.sl_spk->setMaximum(100);
		ui.sl_mic->setValue(100);
		ui.sl_spk->setValue(100);

		ui.le_remoteAddress->setText("127.0.0.1");
		ui.le_remoteAudioPort->setText("60000");
		ui.le_remoteVideoPort->setText("60002");
		ui.le_localAudioPort->setText("60000");
		ui.le_localVideoPort->setText("60002");
		ui.le_remoteAddress->selectAll();
		ui.le_remoteAddress->setFocus();

		connect(ui.action_Quit, SIGNAL(triggered()), SLOT(close()));
		connect(ui.action_Configure, SIGNAL(triggered()), SLOT(doConfigure()));
		connect(ui.action_About, SIGNAL(triggered()), SLOT(doAbout()));
		connect(ui.pb_startSend, SIGNAL(clicked()), SLOT(start_send()));
		connect(ui.pb_transmit, SIGNAL(clicked()), SLOT(transmit()));
		connect(ui.pb_stopSend, SIGNAL(clicked()), SLOT(stop_send()));
		connect(ui.pb_startReceive, SIGNAL(clicked()), SLOT(start_receive()));
		connect(ui.pb_stopReceive, SIGNAL(clicked()), SLOT(stop_receive()));
		connect(ui.pb_record, SIGNAL(clicked()), SLOT(record_toggle()));
		connect(ui.sl_mic, SIGNAL(valueChanged(int)), SLOT(change_volume_mic(int)));
		connect(ui.sl_spk, SIGNAL(valueChanged(int)), SLOT(change_volume_spk(int)));
		connect(&producer, SIGNAL(started()), SLOT(producer_started()));
		connect(&producer, SIGNAL(stopped()), SLOT(producer_stopped()));
		connect(&producer, SIGNAL(finished()), SLOT(producer_finished()));
		connect(&producer, SIGNAL(error()), SLOT(producer_error()));
		connect(&receiver, SIGNAL(started()), SLOT(receiver_started()));
		connect(&receiver, SIGNAL(stoppedRecording()), SLOT(receiver_stoppedRecording()));
		connect(&receiver, SIGNAL(stopped()), SLOT(receiver_stopped()));
		connect(&receiver, SIGNAL(error()), SLOT(receiver_error()));

		// set initial volume levels
		change_volume_mic(ui.sl_mic->value());
		change_volume_spk(ui.sl_spk->value());

		// associate video widgets
		producer.setVideoPreviewWidget(ui.vw_self);
		receiver.setVideoOutputWidget(ui.vw_remote);

		// hack: make the top/bottom layouts have matching height
		int lineEditHeight = ui.le_receiveConfig->sizeHint().height();
		QWidget *spacer = new QWidget(this);
		spacer->setMinimumHeight(lineEditHeight);
		spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		ui.gridLayout2->addWidget(spacer, 3, 1);

		// hack: give the video widgets a 4:3 ratio
		int gridSpacing = ui.gridLayout1->verticalSpacing();
		if(gridSpacing == -1)
			gridSpacing = 9; // not sure how else to get this
		int pushButtonHeight = ui.pb_startSend->sizeHint().height();
		int heightEstimate = lineEditHeight * 4 + pushButtonHeight + gridSpacing * 4;
		heightEstimate += 10; // pad just to be safe
		int goodWidth = (heightEstimate * 4) / 3;
		ui.vw_remote->setMinimumSize(goodWidth, heightEstimate);
		ui.vw_self->setMinimumSize(goodWidth, heightEstimate);
	}

	~MainWin()
	{
		cleanup_send_rtp();
		cleanup_receive_rtp();
		cleanup_record();
	}

	void setSendFieldsEnabled(bool b)
	{
		ui.lb_remoteAddress->setEnabled(b);
		ui.le_remoteAddress->setEnabled(b);
		ui.lb_remoteAudioPort->setEnabled(b);
		ui.le_remoteAudioPort->setEnabled(b);
		ui.lb_remoteVideoPort->setEnabled(b);
		ui.le_remoteVideoPort->setEnabled(b);
	}

	void setSendConfig(const QString &s)
	{
		if(!s.isEmpty())
		{
			ui.lb_sendConfig->setEnabled(true);
			ui.le_sendConfig->setEnabled(true);
			ui.le_sendConfig->setText(s);
			ui.le_sendConfig->setCursorPosition(0);
		}
		else
		{
			ui.lb_sendConfig->setEnabled(false);
			ui.le_sendConfig->setEnabled(false);
			ui.le_sendConfig->clear();
		}
	}

	void setReceiveFieldsEnabled(bool b)
	{
		ui.lb_localAudioPort->setEnabled(b);
		ui.le_localAudioPort->setEnabled(b);
		ui.lb_localVideoPort->setEnabled(b);
		ui.le_localVideoPort->setEnabled(b);
		ui.lb_receiveConfig->setEnabled(b);
		ui.le_receiveConfig->setEnabled(b);
	}

	static QString rtpSessionErrorToString(PsiMedia::RtpSession::Error e)
	{
		QString str;
		switch(e)
		{
			case PsiMedia::RtpSession::ErrorSystem:
				str = tr("System error"); break;
			case PsiMedia::RtpSession::ErrorCodec:
				str = tr("Codec error"); break;
			default: // generic
				str = tr("Generic error"); break;
		}
		return str;
	}

	void cleanup_send_rtp()
	{
		delete sendAudioRtp;
		sendAudioRtp = 0;
		delete sendVideoRtp;
		sendVideoRtp = 0;
	}

	void cleanup_receive_rtp()
	{
		delete receiveAudioRtp;
		receiveAudioRtp = 0;
		delete receiveVideoRtp;
		receiveVideoRtp = 0;
	}

	void cleanup_record()
	{
		if(recording)
		{
			receiver.setRecordingQIODevice(0);
			delete recordFile;
			recordFile = 0;
			recording = false;
		}
	}

private slots:
	void doConfigure()
	{
		ConfigDlg w(config, this);
		w.exec();
		config = w.config;
	}

	void doAbout()
	{
		QMessageBox::about(this, tr("About PsiMedia Test"), tr(
			"PsiMedia Test v1.0\n"
			"A simple test application for the PsiMedia system.\n"
			"\n"
			"Copyright (C) 2008  Barracuda Networks, Inc."
			));
	}

	void doAboutProvider()
	{
		QMessageBox::about(this, tr("About %1").arg(creditName),
			PsiMedia::creditText()
			);
	}

	void start_send()
	{
		config = adjustConfiguration(config, PsiMediaFeaturesSnapshot());

		transmitAudio = false;
		transmitVideo = false;

		if(config.liveInput)
		{
			if(config.audioInDeviceId.isEmpty() && config.videoInDeviceId.isEmpty())
			{
				QMessageBox::information(this, tr("Error"), tr(
					"Cannot send live without at least one audio "
					"input or video input device selected."
					));
				return;
			}

			if(!config.audioInDeviceId.isEmpty())
			{
				producer.setAudioInputDevice(config.audioInDeviceId);
				transmitAudio = true;
			}
			else
				producer.setAudioInputDevice(QString());

			if(!config.videoInDeviceId.isEmpty())
			{
				producer.setVideoInputDevice(config.videoInDeviceId);
				transmitVideo = true;
			}
			else
				producer.setVideoInputDevice(QString());
		}
		else // non-live (file) input
		{
			producer.setFileInput(config.file);
			producer.setFileLoopEnabled(config.loopFile);

			// we just assume the file has both audio and video.
			//   if it doesn't, no big deal, it'll still work.
			//   update: after producer is started, we can correct
			//   these variables.
			transmitAudio = true;
			transmitVideo = true;
		}

		QList<PsiMedia::AudioParams> audioParamsList;
		if(transmitAudio)
			audioParamsList += config.audioParams;
		producer.setLocalAudioPreferences(audioParamsList);

		QList<PsiMedia::VideoParams> videoParamsList;
		if(transmitVideo)
			videoParamsList += config.videoParams;
		producer.setLocalVideoPreferences(videoParamsList);

		ui.pb_startSend->setEnabled(false);
		ui.pb_stopSend->setEnabled(true);
		transmitting = false;
		producer.start();
	}

	void transmit()
	{
		QHostAddress addr;
		if(!addr.setAddress(ui.le_remoteAddress->text()))
		{
			QMessageBox::critical(this, tr("Error"), tr(
				"Invalid send IP address."
				));
			return;
		}

		int audioPort = -1;
		if(transmitAudio)
		{
			bool ok;
			audioPort = ui.le_remoteAudioPort->text().toInt(&ok);
			if(!ok || audioPort < BASE_PORT_MIN || audioPort > BASE_PORT_MAX)
			{
				QMessageBox::critical(this, tr("Error"), tr(
					"Invalid send audio port."
					));
				return;
			}
		}

		int videoPort = -1;
		if(transmitVideo)
		{
			bool ok;
			videoPort = ui.le_remoteVideoPort->text().toInt(&ok);
			if(!ok || videoPort < BASE_PORT_MIN || videoPort > BASE_PORT_MAX)
			{
				QMessageBox::critical(this, tr("Error"), tr(
					"Invalid send video port."
					));
				return;
			}
		}

		RtpSocketGroup *audioSocketGroup = new RtpSocketGroup;
		sendAudioRtp = new RtpBinding(RtpBinding::Send, producer.audioRtpChannel(), audioSocketGroup, this);
		sendAudioRtp->sendAddress = addr;
		sendAudioRtp->sendBasePort = audioPort;

		RtpSocketGroup *videoSocketGroup = new RtpSocketGroup;
		sendVideoRtp = new RtpBinding(RtpBinding::Send, producer.videoRtpChannel(), videoSocketGroup, this);
		sendVideoRtp->sendAddress = addr;
		sendVideoRtp->sendBasePort = videoPort;

		setSendFieldsEnabled(false);
		ui.pb_transmit->setEnabled(false);

		if(transmitAudio)
			producer.transmitAudio();
		if(transmitVideo)
			producer.transmitVideo();

		transmitting = true;
	}

	void stop_send()
	{
		ui.pb_stopSend->setEnabled(false);

		if(!transmitting)
			ui.pb_transmit->setEnabled(false);

		producer.stop();
	}

	void start_receive()
	{
		config = adjustConfiguration(config, PsiMediaFeaturesSnapshot());

		QString receiveConfig = ui.le_receiveConfig->text();
		PsiMedia::PayloadInfo audio;
		PsiMedia::PayloadInfo video;
		if(receiveConfig.isEmpty() || !codecStringToPayloadInfo(receiveConfig, &audio, &video))
		{
			QMessageBox::critical(this, tr("Error"), tr(
				"Invalid codec config."
				));
			return;
		}

		receiveAudio = !audio.isNull();
		receiveVideo = !video.isNull();

		int audioPort = -1;
		if(receiveAudio)
		{
			bool ok;
			audioPort = ui.le_localAudioPort->text().toInt(&ok);
			if(!ok || audioPort < BASE_PORT_MIN || audioPort > BASE_PORT_MAX)
			{
				QMessageBox::critical(this, tr("Error"), tr(
					"Invalid receive audio port."
					));
				return;
			}
		}

		int videoPort = -1;
		if(receiveVideo)
		{
			bool ok;
			videoPort = ui.le_localVideoPort->text().toInt(&ok);
			if(!ok || videoPort < BASE_PORT_MIN || videoPort > BASE_PORT_MAX)
			{
				QMessageBox::critical(this, tr("Error"), tr(
					"Invalid receive video port."
					));
				return;
			}
		}

		if(receiveAudio && !config.audioOutDeviceId.isEmpty())
		{
			receiver.setAudioOutputDevice(config.audioOutDeviceId);

			QList<PsiMedia::AudioParams> audioParamsList;
			audioParamsList += config.audioParams;
			receiver.setLocalAudioPreferences(audioParamsList);

			QList<PsiMedia::PayloadInfo> payloadInfoList;
			payloadInfoList += audio;
			receiver.setRemoteAudioPreferences(payloadInfoList);
		}

		if(receiveVideo)
		{
			QList<PsiMedia::VideoParams> videoParamsList;
			videoParamsList += config.videoParams;
			receiver.setLocalVideoPreferences(videoParamsList);

			QList<PsiMedia::PayloadInfo> payloadInfoList;
			payloadInfoList += video;
			receiver.setRemoteVideoPreferences(payloadInfoList);
		}

		RtpSocketGroup *audioSocketGroup = new RtpSocketGroup(this);
		RtpSocketGroup *videoSocketGroup = new RtpSocketGroup(this);
		if(!audioSocketGroup->bind(audioPort))
		{
			delete audioSocketGroup;
			audioSocketGroup = 0;
			delete videoSocketGroup;
			videoSocketGroup = 0;

			QMessageBox::critical(this, tr("Error"), tr(
				"Unable to bind to receive audio ports."
				));
			return;
		}
		if(!videoSocketGroup->bind(videoPort))
		{
			delete audioSocketGroup;
			audioSocketGroup = 0;
			delete videoSocketGroup;
			videoSocketGroup = 0;

			QMessageBox::critical(this, tr("Error"), tr(
				"Unable to bind to receive video ports."
				));
			return;
		}

		receiveAudioRtp = new RtpBinding(RtpBinding::Receive, receiver.audioRtpChannel(), audioSocketGroup, this);
		receiveVideoRtp = new RtpBinding(RtpBinding::Receive, receiver.videoRtpChannel(), videoSocketGroup, this);

		setReceiveFieldsEnabled(false);
		ui.pb_startReceive->setEnabled(false);
		ui.pb_stopReceive->setEnabled(true);
		receiver.start();
	}

	void stop_receive()
	{
		ui.pb_stopReceive->setEnabled(false);
		receiver.stop();
	}

	void change_volume_mic(int value)
	{
		producer.setInputVolume(value);
	}

	void change_volume_spk(int value)
	{
		receiver.setOutputVolume(value);
	}

	void producer_started()
	{
		PsiMedia::PayloadInfo audio, *pAudio;
		PsiMedia::PayloadInfo video, *pVideo;

		pAudio = 0;
		pVideo = 0;
		if(transmitAudio)
		{
			// confirm transmitting of audio is actually possible,
			//   in the case that a file is used as input
			if(producer.canTransmitAudio())
			{
				audio = producer.localAudioPayloadInfo().first();
				pAudio = &audio;
			}
			else
				transmitAudio = false;
		}
		if(transmitVideo)
		{
			// same for video
			if(producer.canTransmitVideo())
			{
				video = producer.localVideoPayloadInfo().first();
				pVideo = &video;
			}
			else
				transmitVideo = false;
		}

		QString str = payloadInfoToCodecString(pAudio, pVideo);
		setSendConfig(str);

		ui.pb_transmit->setEnabled(true);
	}

	void producer_stopped()
	{
		cleanup_send_rtp();

		setSendFieldsEnabled(true);
		setSendConfig(QString());
		ui.pb_startSend->setEnabled(true);
	}

	void producer_finished()
	{
		cleanup_send_rtp();

		setSendFieldsEnabled(true);
		setSendConfig(QString());
		ui.pb_startSend->setEnabled(true);
		ui.pb_transmit->setEnabled(false);
		ui.pb_stopSend->setEnabled(false);
	}

	void producer_error()
	{
		cleanup_send_rtp();

		setSendFieldsEnabled(true);
		setSendConfig(QString());
		ui.pb_startSend->setEnabled(true);
		ui.pb_transmit->setEnabled(false);
		ui.pb_stopSend->setEnabled(false);

		QMessageBox::critical(this, tr("Error"), tr(
			"An error occurred while trying to send:\n%1."
			).arg(rtpSessionErrorToString(producer.errorCode())
			));
	}

	void receiver_started()
	{
		ui.pb_record->setEnabled(true);
	}

	void receiver_stoppedRecording()
	{
		cleanup_record();
	}

	void receiver_stopped()
	{
		cleanup_receive_rtp();
		cleanup_record();

		setReceiveFieldsEnabled(true);
		ui.pb_startReceive->setEnabled(true);
		ui.pb_record->setEnabled(false);
	}

	void receiver_error()
	{
		cleanup_receive_rtp();
		cleanup_record();

		setReceiveFieldsEnabled(true);
		ui.pb_startReceive->setEnabled(true);
		ui.pb_stopReceive->setEnabled(false);
		ui.pb_record->setEnabled(false);

		QMessageBox::critical(this, tr("Error"), tr(
			"An error occurred while trying to receive:\n%1."
			).arg(rtpSessionErrorToString(receiver.errorCode())
			));
	}

	void record_toggle()
	{
		if(!recording)
		{
			QString fileName = QFileDialog::getSaveFileName(this,
				tr("Save File"),
				QDir::homePath(),
				tr("Ogg Audio/Video (*.oga *.ogv)"));
			if(fileName.isEmpty())
				return;

			recordFile = new QFile(fileName, this);
			if(!recordFile->open(QIODevice::WriteOnly | QIODevice::Truncate))
			{
				delete recordFile;

				QMessageBox::critical(this, tr("Error"), tr(
					"Unable to create file for recording."
					));
				return;
			}

			receiver.setRecordingQIODevice(recordFile);
			recording = true;
		}
		else
			receiver.stopRecording();
	}
};

#ifdef GSTPROVIDER_STATIC
Q_IMPORT_PLUGIN(gstprovider)
#endif

#ifndef GSTPROVIDER_STATIC
static QString findPlugin(const QString &relpath, const QString &basename)
{
	QDir dir(QCoreApplication::applicationDirPath());
	if(!dir.cd(relpath))
		return QString();
	foreach(const QString &fileName, dir.entryList())
	{
		if(fileName.contains(basename))
		{
			QString filePath = dir.filePath(fileName);
			if(QLibrary::isLibrary(filePath))
				return filePath;
		}
	}
	return QString();
}
#endif

int main(int argc, char **argv)
{
	QApplication qapp(argc, argv);

#ifndef GSTPROVIDER_STATIC
	QString pluginFile = findPlugin("../gstprovider", "gstprovider");
# ifdef GSTBUNDLE_PATH
	PsiMedia::loadPlugin(pluginFile, GSTBUNDLE_PATH);
# else
	PsiMedia::loadPlugin(pluginFile, QString());
# endif
#endif

	if(!PsiMedia::isSupported())
	{
		QMessageBox::critical(0, MainWin::tr("PsiMedia Test"),
			MainWin::tr(
			"Error: Could not load PsiMedia subsystem."
			));
		return 1;
	}

	MainWin mainWin;

	// give mainWin a chance to fix its layout before showing
	QTimer::singleShot(0, &mainWin, SLOT(show()));

	qapp.exec();
	return 0;
}

#include "main.moc"
