# PsiMedia

PsiMedia is a thick abstraction layer for providing audio and video RTP services to Psi-like IM clients. The implementation is based on GStreamer.

For more information, see [this article](http://jblog.andbit.net/2008/07/03/introducing-psimedia/).

## License

This library is licensed under the Lesser GNU General Public License. See the COPYING file for more information.

## Usage

Contents:

```
psimedia/      API and plugin shim
gstprovider/   provider plugin based on GStreamer
demo/          demonstration GUI program
```

To build the plugin and demo program, run:

```sh
./configure
make
```

There is no "make install".  The compiled plugin can be found under the gstprovider directory. An application that uses PsiMedia should have instructions on what to do with the plugin.
