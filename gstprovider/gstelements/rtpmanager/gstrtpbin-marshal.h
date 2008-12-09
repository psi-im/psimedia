
#ifndef __gst_rtp_bin_marshal_MARSHAL_H__
#define __gst_rtp_bin_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* UINT:UINT (gstrtpbin-marshal.list:1) */
extern void gst_rtp_bin_marshal_UINT__UINT (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* BOXED:UINT (gstrtpbin-marshal.list:2) */
extern void gst_rtp_bin_marshal_BOXED__UINT (GClosure     *closure,
                                             GValue       *return_value,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      invocation_hint,
                                             gpointer      marshal_data);

/* BOXED:UINT,UINT (gstrtpbin-marshal.list:3) */
extern void gst_rtp_bin_marshal_BOXED__UINT_UINT (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* OBJECT:UINT (gstrtpbin-marshal.list:4) */
extern void gst_rtp_bin_marshal_OBJECT__UINT (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* VOID:UINT,OBJECT (gstrtpbin-marshal.list:5) */
extern void gst_rtp_bin_marshal_VOID__UINT_OBJECT (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* VOID:UINT,UINT (gstrtpbin-marshal.list:6) */
extern void gst_rtp_bin_marshal_VOID__UINT_UINT (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);

/* VOID:OBJECT,OBJECT (gstrtpbin-marshal.list:7) */
extern void gst_rtp_bin_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                                     GValue       *return_value,
                                                     guint         n_param_values,
                                                     const GValue *param_values,
                                                     gpointer      invocation_hint,
                                                     gpointer      marshal_data);

G_END_DECLS

#endif /* __gst_rtp_bin_marshal_MARSHAL_H__ */

