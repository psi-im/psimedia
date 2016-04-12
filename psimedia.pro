TEMPLATE = subdirs

#sub_gstelements.subdir = gstprovider/gstelements/static

sub_demo.subdir = demo

sub_gstprovider.subdir = gstprovider
#sub_gstprovider.depends = sub_gstelements

#SUBDIRS += sub_gstelements
SUBDIRS += sub_demo

SUBDIRS += sub_gstprovider
