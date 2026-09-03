#include "lfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    uint64_t size;
} image_context_t;

static int image_read(const struct lfs_config *config, lfs_block_t block,
                      lfs_off_t offset, void *buffer, lfs_size_t size) {
    const image_context_t *image = config->context;
    const uint64_t absolute = (uint64_t)block * config->block_size + offset;
    if (absolute > image->size || size > image->size - absolute) {
        return LFS_ERR_IO;
    }

    size_t completed = 0;
    while (completed < size) {
        const ssize_t result =
            pread(image->fd, (uint8_t *)buffer + completed, size - completed,
                  (off_t)(absolute + completed));
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return LFS_ERR_IO;
        }
        completed += (size_t)result;
    }
    return LFS_ERR_OK;
}

static bool safe_name(const char *name) {
    return name[0] != '\0' && strcmp(name, ".") != 0 &&
           strcmp(name, "..") != 0 && strchr(name, '/') == NULL;
}

static char *join_path(const char *base, const char *name, bool root) {
    const size_t base_length = strlen(base);
    const size_t name_length = strlen(name);
    const bool needs_separator =
        !root && base_length > 0 && base[base_length - 1] != '/';
    const size_t length =
        base_length + (needs_separator ? 1 : 0) + name_length + 1;
    char *result = malloc(length);
    if (result == NULL) {
        return NULL;
    }
    (void)snprintf(result, length, "%s%s%s", base, needs_separator ? "/" : "",
                   name);
    return result;
}

static int ensure_directory(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    if (errno != EEXIST) {
        fprintf(stderr, "littlefs-extract: mkdir %s failed: %s\n", path,
                strerror(errno));
        return -1;
    }

    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode)) {
        fprintf(stderr,
                "littlefs-extract: output path is not a directory: %s\n", path);
        return -1;
    }
    return 0;
}

static int write_all(int fd, const uint8_t *buffer, size_t size) {
    size_t completed = 0;
    while (completed < size) {
        const ssize_t result = write(fd, buffer + completed, size - completed);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return -1;
        }
        completed += (size_t)result;
    }
    return 0;
}

static int extract_file(lfs_t *filesystem, const char *lfs_path,
                        const char *host_path, lfs_size_t expected_size) {
    lfs_file_t file = {0};
    int result = lfs_file_open(filesystem, &file, lfs_path, LFS_O_RDONLY);
    if (result < 0) {
        fprintf(stderr, "littlefs-extract: open %s failed: %d\n", lfs_path,
                result);
        return -1;
    }

    int output_flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
    output_flags |= O_NOFOLLOW;
#endif
    const int output = open(host_path, output_flags, 0666);
    if (output < 0) {
        fprintf(stderr, "littlefs-extract: open %s failed: %s\n", host_path,
                strerror(errno));
        (void)lfs_file_close(filesystem, &file);
        return -1;
    }

    uint8_t buffer[4096];
    lfs_size_t extracted = 0;
    for (;;) {
        const lfs_ssize_t count =
            lfs_file_read(filesystem, &file, buffer, sizeof(buffer));
        if (count < 0) {
            fprintf(stderr, "littlefs-extract: read %s failed: %d\n", lfs_path,
                    (int)count);
            result = -1;
            break;
        }
        if (count == 0) {
            result = 0;
            break;
        }
        if (write_all(output, buffer, (size_t)count) != 0) {
            fprintf(stderr, "littlefs-extract: write %s failed: %s\n",
                    host_path, strerror(errno));
            result = -1;
            break;
        }
        extracted += (lfs_size_t)count;
    }

    if (close(output) != 0 && result == 0) {
        fprintf(stderr, "littlefs-extract: close %s failed: %s\n", host_path,
                strerror(errno));
        result = -1;
    }
    const int close_result = lfs_file_close(filesystem, &file);
    if (close_result < 0 && result == 0) {
        fprintf(stderr, "littlefs-extract: close %s failed: %d\n", lfs_path,
                close_result);
        result = -1;
    }
    if (result == 0 && extracted != expected_size) {
        fprintf(stderr,
                "littlefs-extract: size mismatch for %s: expected %u, got %u\n",
                lfs_path, (unsigned)expected_size, (unsigned)extracted);
        result = -1;
    }
    return result;
}

static int extract_directory(lfs_t *filesystem, const char *lfs_path,
                             const char *host_path) {
    lfs_dir_t directory = {0};
    int result = lfs_dir_open(filesystem, &directory, lfs_path);
    if (result < 0) {
        fprintf(stderr, "littlefs-extract: open directory %s failed: %d\n",
                lfs_path, result);
        return -1;
    }

    struct lfs_info info;
    while ((result = lfs_dir_read(filesystem, &directory, &info)) > 0) {
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }
        if (!safe_name(info.name)) {
            fprintf(stderr, "littlefs-extract: unsafe entry name in %s\n",
                    lfs_path);
            result = -1;
            break;
        }

        char *child_lfs =
            join_path(lfs_path, info.name, strcmp(lfs_path, "/") == 0);
        char *child_host = join_path(host_path, info.name, false);
        if (child_lfs == NULL || child_host == NULL) {
            fprintf(stderr, "littlefs-extract: out of memory\n");
            free(child_lfs);
            free(child_host);
            result = -1;
            break;
        }

        if (info.type == LFS_TYPE_DIR) {
            result = ensure_directory(child_host);
            if (result == 0) {
                result = extract_directory(filesystem, child_lfs, child_host);
            }
        } else if (info.type == LFS_TYPE_REG) {
            result = extract_file(filesystem, child_lfs, child_host, info.size);
        } else {
            fprintf(stderr, "littlefs-extract: unsupported entry type for %s\n",
                    child_lfs);
            result = -1;
        }

        free(child_lfs);
        free(child_host);
        if (result != 0) {
            break;
        }
    }

    if (result < 0) {
        fprintf(stderr, "littlefs-extract: read directory %s failed: %d\n",
                lfs_path, result);
    } else {
        result = 0;
    }
    const int close_result = lfs_dir_close(filesystem, &directory);
    if (close_result < 0 && result == 0) {
        fprintf(stderr, "littlefs-extract: close directory %s failed: %d\n",
                lfs_path, close_result);
        result = -1;
    }
    return result;
}

static bool parse_u32(const char *value, uint32_t *parsed) {
    char *end = NULL;
    errno = 0;
    const unsigned long result = strtoul(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || result > UINT32_MAX) {
        return false;
    }
    *parsed = (uint32_t)result;
    return true;
}

int main(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr,
                "usage: %s IMAGE OUTPUT BLOCK_SIZE READ_SIZE PROGRAM_SIZE "
                "CACHE_SIZE LOOKAHEAD_SIZE\n",
                argv[0]);
        return 2;
    }

    uint32_t block_size;
    uint32_t read_size;
    uint32_t program_size;
    uint32_t cache_size;
    uint32_t lookahead_size;
    if (!parse_u32(argv[3], &block_size) || !parse_u32(argv[4], &read_size) ||
        !parse_u32(argv[5], &program_size) ||
        !parse_u32(argv[6], &cache_size) ||
        !parse_u32(argv[7], &lookahead_size) || block_size == 0) {
        fprintf(stderr, "littlefs-extract: invalid filesystem geometry\n");
        return 2;
    }

    const int image_fd = open(argv[1], O_RDONLY);
    if (image_fd < 0) {
        fprintf(stderr, "littlefs-extract: open %s failed: %s\n", argv[1],
                strerror(errno));
        return 1;
    }
    struct stat image_status;
    if (fstat(image_fd, &image_status) != 0 || image_status.st_size <= 0 ||
        (uint64_t)image_status.st_size % block_size != 0) {
        fprintf(
            stderr,
            "littlefs-extract: image size is not a positive block multiple\n");
        (void)close(image_fd);
        return 1;
    }
    if (ensure_directory(argv[2]) != 0) {
        (void)close(image_fd);
        return 1;
    }

    image_context_t image = {
        .fd = image_fd,
        .size = (uint64_t)image_status.st_size,
    };
    const struct lfs_config config = {
        .context = &image,
        .read = image_read,
        .read_size = read_size,
        .prog_size = program_size,
        .block_size = block_size,
        .block_count = (lfs_size_t)(image.size / block_size),
        .block_cycles = 512,
        .cache_size = cache_size,
        .lookahead_size = lookahead_size,
    };
    lfs_t filesystem = {0};
    int result = lfs_mount(&filesystem, &config);
    if (result < 0) {
        fprintf(stderr, "littlefs-extract: mount failed: %d\n", result);
        (void)close(image_fd);
        return 1;
    }

    result = extract_directory(&filesystem, "/", argv[2]);
    const int unmount_result = lfs_unmount(&filesystem);
    if (unmount_result < 0 && result == 0) {
        fprintf(stderr, "littlefs-extract: unmount failed: %d\n",
                unmount_result);
        result = -1;
    }
    if (close(image_fd) != 0 && result == 0) {
        fprintf(stderr, "littlefs-extract: close image failed: %s\n",
                strerror(errno));
        result = -1;
    }
    return result == 0 ? 0 : 1;
}
