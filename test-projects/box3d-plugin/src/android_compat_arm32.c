/*
 * Chỉ dùng khi build armeabi-v7a bằng zig (libc musl).
 * musl 32-bit dùng time_t 64 bit và đổi tên clock_gettime thành __clock_gettime64; Bionic (Android) 32-bit
 * không có symbol này mà chỉ có clock_gettime với struct timespec 32 bit. Hàm dưới chuyển tiếp sang Bionic.
 * Không include header hệ thống để tránh khai báo của musl.
 */
typedef int int32_t_;
typedef long long int64_t_;

struct bionic_timespec { int32_t_ tv_sec; int32_t_ tv_nsec; };
/* musl little-endian 32-bit: time_t (8 byte), long tv_nsec (4 byte), 4 byte đệm */
struct musl_timespec64 { int64_t_ tv_sec; int32_t_ tv_nsec; int32_t_ pad; };

extern int clock_gettime( int clk, struct bionic_timespec * ts );

__attribute__( ( visibility( "hidden" ) ) ) int __clock_gettime64( int clk, struct musl_timespec64 * out )
{
    struct bionic_timespec t;
    int r = clock_gettime( clk, &t );

    out->tv_sec = t.tv_sec;
    out->tv_nsec = t.tv_nsec;
    out->pad = 0;
    return r;
}
