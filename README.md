# PsiMedia

PsiMedia is a thick abstraction layer for providing audio and video RTP services to Psi-like IM clients. The implementation is based on GStreamer.

For more information, see [this article](http://jblog.andbit.net/2008/07/03/introducing-psimedia/).

Currently it is used for video- and audio-calls support in [Psi IM](https://psi-im.org/) and [Psi+](https://psi-plus.com/) projects.

## License

This library is licensed under the Lesser GNU General Public License. See the [COPYING](https://github.com/psi-im/psimedia/blob/master/COPYING) file for more information.

## Versions history

See [CHANGELOG](https://github.com/psi-im/psimedia/blob/master/CHANGELOG) file.

## Build dependencies

* [qconf](https://github.com/psi-plus/qconf) (optional)
* qtbase >= 5.6
* glib >= 2.0
* gobject >= 2.0
* gthread >= 2.0
* gstreamer >= 1.10.4
* gst-plugins-base >= 1.10.4

## Installation

Contents:

```
psimedia/      API and plugin shim
gstprovider/   provider plugin based on GStreamer
demo/          demonstration GUI program
```

To build the plugin and demo program, run:

```sh
qt-qconf
./configure
make
```

There is no `make install` target in this case. The compiled plugin may be found under the `gstprovider` directory. An application that uses PsiMedia should have instructions on what to do with the plugin.

For example, in Psi+ program `gstprovider` plugin should be placed into:

* `/usr/lib/psi-plus/plugins/` in GNU/Linux systems
* the root Psi+ directory on MS Windows systems (for example, `C:\\Program Files\Psi+\`)

If you want to test demo program, use environment variable `PSI_MEDIA_PLUGIN` for setting the path to gstprovider plugin. For example:

```
PSI_MEDIA_PLUGIN=/usr/lib/psi-plus/plugins/libgstprovider.so ./demo
```

Alternatively you may build plugin and demo using `cmake`:

```sh
mkdir -p builddir
cd builddir
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
make
```

There is special "make install" target in this case oriented to Psi IM users, see:

```
make install DESTDIR=./out
tree ./out
```

