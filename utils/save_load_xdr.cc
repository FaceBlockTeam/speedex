#include "utils/save_load_xdr.h"

namespace speedex
{

constexpr static auto FILE_PERMISSIONS
    = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

unique_fd
preallocate_file(const char* filename, size_t size)
{
#ifdef __APPLE__

    unique_fd fd{ open(filename, O_CREAT | O_RDONLY, FILE_PERMISSIONS) };
    return fd;

#else

    unique_fd fd{ open(
        filename, O_CREAT | O_WRONLY | O_DIRECT, FILE_PERMISSIONS) };

    if (size == 0)
    {
        return fd;
    }
    auto res = fallocate(fd.get(), 0, 0, size);
    if (res)
    {
        threrror("fallocate");
    }
    return fd;

#endif
}


void 
save_account_block_fast(
    const AccountModificationBlock& value, 
    unique_fd& fd, 
    unsigned char* buffer, 
    const unsigned int BUF_SIZE)
{

    unsigned int buf_idx = 0;

    void* buf_head = static_cast<void*>(buffer);

    size_t aligned_buf_size = BUF_SIZE;

    unsigned char* aligned_buf = reinterpret_cast<unsigned char*>(std::align(512, sizeof(unsigned char), buf_head, aligned_buf_size));

    uint32_t* aligned_buf_cast = reinterpret_cast<uint32_t*>(aligned_buf);

    aligned_buf_size -= (aligned_buf_size % 4);
    aligned_buf_size -= 512; // ensure space for last few bits

    size_t first_odirect_block_sz = 1024;
    uint8_t first_odirect_block[1024];
    bool first_set = false;

    void* first_head = static_cast<void*>(first_odirect_block);

    uint8_t* aligned_first = reinterpret_cast<uint8_t*>(
        std::align(512, sizeof(uint8_t), first_head, first_odirect_block_sz));
    uint32_t* aligned_first_reinterpreted = reinterpret_cast<uint32_t*>(aligned_first);


    size_t list_idx = 0;

    xdr::xdr_put p (aligned_buf, aligned_buf + aligned_buf_size);

    p.put32(aligned_buf_cast, xdr::size32(0)); //space for now
    p.p_ ++; // have to use if not using p.operator() mechanism
    // alternative would have been to just use p.put32(p.p_) or
    // just p(0)
    buf_idx += 4;

    size_t total_written_bytes = 0;

    const xdr::xvector<SignedTransaction>* tx_buffer = nullptr;
    size_t buffer_idx = 0;
    size_t num_written = 0;

    while (list_idx < value.size()) {

        while (tx_buffer == nullptr) {
            if (value[list_idx].new_transactions_self.size() != 0) {
                tx_buffer = &(value[list_idx].new_transactions_self);
                buffer_idx = 0;
            }
            list_idx ++;
            if (list_idx >= value.size()) {
                break;
            }
        }

        if (tx_buffer == nullptr) {
            break;
        }

        auto& next_tx_to_write = (*tx_buffer)[buffer_idx];

        size_t next_sz = xdr::xdr_argpack_size(next_tx_to_write);

        if (aligned_buf_size - buf_idx < next_sz) {

            size_t write_amount = buf_idx - (buf_idx % 512);

            if (!first_set) {
                first_set = true;
                memcpy(aligned_first, aligned_buf, 512);
            }

            flush_buffer(fd, aligned_buf, write_amount);
            total_written_bytes += write_amount;
            memcpy(aligned_buf, aligned_buf + write_amount, buf_idx % 512);
            buf_idx %= 512;

            p.p_ = reinterpret_cast<uint32_t*>(aligned_buf + buf_idx); // reset xdr_put obj
        }

        p(next_tx_to_write);
        num_written ++;
        buffer_idx++;
        if (buffer_idx >= tx_buffer -> size()) {
            tx_buffer = nullptr;
        }

        buf_idx += next_sz;
    }

    if (!first_set) {
        first_set = true;
        memcpy(aligned_first, aligned_buf, 512);
    }

    size_t write_amount = buf_idx - (buf_idx % 512) + 512;
    flush_buffer(fd, (aligned_buf), write_amount);

    total_written_bytes += buf_idx;

    auto res2 = lseek(fd.get(), 0, SEEK_SET);
    if (res2 != 0) {
        threrror("lseek");
    }

    p.put32(aligned_first_reinterpreted, xdr::size32(num_written));
    flush_buffer(fd, aligned_first, 512);

    auto res = ftruncate(fd.get(), total_written_bytes);

    if (res) {
        threrror("ftruncate");
    }
    
    fsync(fd.get());
}

//! make a new directory, does not throw error if dir already exists.
bool
mkdir_safe(const char* dirname)
{
    constexpr static auto mkdir_perms = S_IRWXU | S_IRWXG | S_IRWXO;

    auto res = mkdir(dirname, mkdir_perms);
    if (res == 0)
    {
        return false;
    }

    if (errno == EEXIST)
    {
        return true;
    }
    threrror(std::string("mkdir ") + std::string(dirname));
}

} // namespace speedex