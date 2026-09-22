// SPDX-License-Identifier: LGPL-2.1-or-later

#include "../gstprovider/bins.h"

#include <QCoreApplication>
#include <QSize>
#include <QtGlobal>

#include <gst/gst.h>

namespace {

bool hasFactory(GstElement *element, const char *factoryName)
{
    if (!GST_IS_BIN(element))
        return false;

    GstIterator *iterator = gst_bin_iterate_elements(GST_BIN(element));
    GValue item = G_VALUE_INIT;
    bool found = false;
    while (gst_iterator_next(iterator, &item) == GST_ITERATOR_OK) {
        auto *child = GST_ELEMENT(g_value_get_object(&item));
        GstElementFactory *factory = gst_element_get_factory(child);
        const gchar *name = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : nullptr;
        if (name && qstrcmp(name, factoryName) == 0) {
            found = true;
            g_value_reset(&item);
            break;
        }
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(iterator);
    return found;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    gst_init(nullptr, nullptr);

    GstElement *nativeCadence = PsiMedia::bins_videoprep_create(QSize(640, 480), -1, true);
    if (!nativeCadence)
        qFatal("Could not construct native-cadence video prep");
    if (hasFactory(nativeCadence, "videorate"))
        qFatal("Unspecified live FPS unexpectedly inserts videorate");
    gst_object_unref(nativeCadence);

    GstElement *forcedCadence = PsiMedia::bins_videoprep_create(QSize(640, 480), 30, true);
    if (!forcedCadence)
        qFatal("Could not construct explicit-cadence video prep");
    if (!hasFactory(forcedCadence, "videorate"))
        qFatal("Explicit FPS did not insert videorate");
    gst_object_unref(forcedCadence);

    qInfo("Live video cadence regression passed");
    return 0;
}
