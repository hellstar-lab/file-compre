/* Minimal export header to satisfy miniz.h includes when vendored.
 * If the upstream provides a richer export header, it can replace this.
 */
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif

#ifndef MZ_EXTERN_C
#ifdef __cplusplus
#define MZ_EXTERN_C extern "C"
#else
#define MZ_EXTERN_C
#endif
#endif

#endif /* MINIZ_EXPORT_H */