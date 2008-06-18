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
#include "psimedia.h"
#include "ui_mainwin.h"
#include "ui_config.h"

/*static QString payloadInfoToString(const PayloadInfo &info)
{
}

static PayloadInfo stringToPayloadInfo()
{
}*/

class ConfigDlg : public QDialog
{
	Q_OBJECT

public:
	Ui::Config ui;

	ConfigDlg(QWidget *parent = 0) :
		QDialog(parent)
	{
		ui.setupUi(this);
		setWindowTitle(tr("Configure Audio/Video"));

		ui.cb_audioOutDevice->addItem("<None>");
		foreach(const PsiMedia::Device &dev, PsiMedia::audioOutputDevices())
			ui.cb_audioOutDevice->addItem(dev.name());

		ui.cb_audioInDevice->addItem("<None>");
		foreach(const PsiMedia::Device &dev, PsiMedia::audioInputDevices())
			ui.cb_audioInDevice->addItem(dev.name());

		foreach(const PsiMedia::AudioParams &params, PsiMedia::supportedAudioModes())
		{
			QString codec = params.codec();
			codec[0] = codec[0].toUpper();
			QString hz = QString::number(params.sampleRate() / 1000);
			QString chanstr;
			if(params.channels() == 1)
				chanstr = "Mono";
			else if(params.channels() == 2)
				chanstr = "Stereo";
			else
				chanstr = QString("Channels: %1").arg(params.channels());
			QString str = QString("%1, %2KHz, %3-bit, %4").arg(codec).arg(hz).arg(params.sampleSize()).arg(chanstr);
			ui.cb_audioInMode->addItem(str);
		}

		ui.cb_videoInDevice->addItem("<None>");
		foreach(const PsiMedia::Device &dev, PsiMedia::videoInputDevices())
			ui.cb_videoInDevice->addItem(dev.name());

		foreach(const PsiMedia::VideoParams &params, PsiMedia::supportedVideoModes())
		{
			QString codec = params.codec();
			codec[0] = codec[0].toUpper();
			QString sizestr = QString("%1x%2").arg(params.size().width()).arg(params.size().height());
			QString str = QString("%1, %2 @ %3fps").arg(codec).arg(sizestr).arg(params.fps());
			ui.cb_videoInMode->addItem(str);
		}
	}
};

class MainWin : public QMainWindow
{
	Q_OBJECT

public:
	Ui::MainWin ui;
	PsiMedia::Receiver receiver;
	PsiMedia::Producer producer;

	MainWin() :
		receiver(this),
		producer(this)
	{
		ui.setupUi(this);
		setWindowTitle(tr("PsiMedia Test"));

		ui.pb_transmit->setEnabled(false);
		ui.pb_stopSend->setEnabled(false);
		ui.pb_stopReceive->setEnabled(false);
		ui.le_sendConfig->setReadOnly(true);
		ui.lb_sendConfig->setEnabled(false);
		ui.le_sendConfig->setEnabled(false);
		ui.sl_mic->setMinimum(0);
		ui.sl_mic->setMaximum(100);
		ui.sl_spk->setMinimum(0);
		ui.sl_spk->setMaximum(100);
		ui.sl_mic->setValue(90);
		ui.sl_spk->setValue(90);

		connect(ui.action_Quit, SIGNAL(triggered()), SLOT(close()));
		connect(ui.action_Configure, SIGNAL(triggered()), SLOT(doConfigure()));
		connect(ui.action_About, SIGNAL(triggered()), SLOT(doAbout()));
		connect(ui.pb_startSend, SIGNAL(clicked()), SLOT(start_send()));
		connect(ui.pb_transmit, SIGNAL(clicked()), SLOT(transmit()));
		connect(ui.pb_stopSend, SIGNAL(clicked()), SLOT(stop_send()));
		connect(ui.pb_startReceive, SIGNAL(clicked()), SLOT(start_receive()));
		connect(ui.pb_stopReceive, SIGNAL(clicked()), SLOT(stop_receive()));
		connect(ui.sl_mic, SIGNAL(valueChanged(int)), SLOT(change_volume_mic(int)));
		connect(ui.sl_spk, SIGNAL(valueChanged(int)), SLOT(change_volume_spk(int)));
		connect(&producer, SIGNAL(started()), SLOT(producer_started()));
		connect(&producer, SIGNAL(stopped()), SLOT(producer_stopped()));
		connect(&producer, SIGNAL(error()), SLOT(producer_error()));
		connect(&receiver, SIGNAL(started()), SLOT(receiver_started()));
		connect(&receiver, SIGNAL(stopped()), SLOT(receiver_stopped()));
		connect(&receiver, SIGNAL(error()), SLOT(receiver_error()));

		// set initial volume levels
		change_volume_mic(ui.sl_mic->value());
		change_volume_spk(ui.sl_spk->value());

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

private slots:
	void doConfigure()
	{
		ConfigDlg w(this);
		w.exec();
	}

	void doAbout()
	{
		QMessageBox::about(this, tr("About PsiMedia Test"), tr(
			"PsiMedia Test v0.1\n"
			"A simple test application for the PsiMedia system.\n"
			"\n"
			"Copyright (C) 2008  Barracuda Networks, Inc."
			));
	}

	void start_send()
	{
		// TODO: configure producer

		ui.pb_startSend->setEnabled(false);
		ui.pb_stopSend->setEnabled(true);
		producer.start();

		QTimer::singleShot(1000, this, SLOT(producer_started()));
	}

	void transmit()
	{
		setSendFieldsEnabled(false);
		ui.pb_transmit->setEnabled(false);
	}

	void stop_send()
	{
		// TODO
		ui.pb_stopSend->setEnabled(false);

		// TODO: if !transmitting { }
		ui.pb_transmit->setEnabled(false);

		QTimer::singleShot(1000, this, SLOT(producer_stopped()));
	}

	void start_receive()
	{
		// TODO: configure receiver

		setReceiveFieldsEnabled(false);
		ui.pb_startReceive->setEnabled(false);
		ui.pb_stopReceive->setEnabled(true);
		//receiver.start();

		QTimer::singleShot(1000, this, SLOT(receiver_started()));
	}

	void stop_receive()
	{
		// TODO

		ui.pb_stopReceive->setEnabled(false);

		QTimer::singleShot(1000, this, SLOT(receiver_stopped()));
	}

	void change_volume_mic(int value)
	{
		producer.setVolume(value);
	}

	void change_volume_spk(int value)
	{
		receiver.setVolume(value);
	}

	void producer_started()
	{
		// TODO: populate ui.le_sendConfig with producer config
		setSendConfig("AAAAAVwurg/HAh4BMQF2b3JiaXMAAAAAAkSsAAAAAAAAgLUBAAAAAAC4AQN2b3JiaXMdAAAAWGlwaC5PcmcgbGliVm9yYmlzIEkgMjAwNzA2MjIHAAAAEgAAAE");

		ui.pb_transmit->setEnabled(true);
	}

	void producer_stopped()
	{
		setSendFieldsEnabled(true);
		setSendConfig(QString());
		ui.pb_startSend->setEnabled(true);
	}

	void producer_error()
	{
		setSendFieldsEnabled(true);
		setSendConfig(QString());
		ui.pb_startSend->setEnabled(true);
		ui.pb_transmit->setEnabled(false);
		ui.pb_stopSend->setEnabled(false);

		// TODO: show error
	}

	void receiver_started()
	{
		// TODO
	}

	void receiver_stopped()
	{
		// TODO
	}

	void receiver_error()
	{
		// TODO
	}
};

int main(int argc, char **argv)
{
	QApplication qapp(argc, argv);
	MainWin mainWin;

	// give mainWin a chance to fix its layout before showing
	QTimer::singleShot(0, &mainWin, SLOT(show()));

	qapp.exec();
	return 0;
}

#include "main.moc"
