
#include "buf0nvme_hint.h"

#include "buf0buf.h"
#include "fil0fil.h"
#include "srv0srv.h"

struct fiemap *get_fiemap(int fd, uint64_t offset, uint64_t size) {
  struct fiemap *fiemap = (struct fiemap *)calloc(
      1, sizeof(struct fiemap) + sizeof(struct fiemap_extent));
  if (fiemap == NULL) {
    return NULL;
  }
  fiemap->fm_start = offset;
  fiemap->fm_length = size;
  fiemap->fm_extent_count = 1;
  fiemap->fm_flags = FIEMAP_FLAG_SYNC;

  if (ioctl(fd, FS_IOC_FIEMAP, fiemap)) {
    free(fiemap);
    fiemap = NULL;
  }
  return fiemap;
}

void nvme_send_host_hint(buf_page_t *bpage, uint32_t flag) {
  if (!srv_use_nvme_hint) {
    return;
  }
  if (bpage == nullptr || !buf_page_in_file(bpage)) {
    return;
  }
  fil_space_t *space = fil_space_get(bpage->id.space());

  if (space == nullptr) {
    return;
  }

  const char *path = space->name;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return;
  }
  int nvme_fd = open("/dev/nvme0n1", O_RDWR);
  if (nvme_fd < 0) {
    close(fd);
    return;
  }

  const ulint page_size_phys = bpage->size.physical();
  const off_t offset = (off_t)bpage->id.page_no() * page_size_phys;

  struct fiemap *map = get_fiemap(fd, offset, page_size_phys);
  if (map == NULL) {
    close(nvme_fd);
    return;
  }
  struct nvme_dsm_range dsm_range;
  struct nvme_dsm_args args;
  const uint64_t sector_size = 512;
  uint32_t i;

  memset(&args, 0, sizeof(args));
  memset(&dsm_range, 0, sizeof(dsm_range));
  dsm_range.cattr = (flag);
  args.args_size = sizeof(args);
  args.fd = nvme_fd;
  args.timeout = 0;
  args.nsid = 1;
  args.nr_ranges = 1;
  args.dsm = &dsm_range;
  args.attrs = DSM_ATTR_BUFFER_HINT;
  args.result = (uint32_t *)malloc(sizeof(uint32_t));
  if (!args.result) {
    fprintf(stderr, "nvme_dsm failed allocating result buffer: %s\n",
            strerror(errno));
    free(map);
    close(nvme_fd);
    return;
  }

  for (i = 0; i < map->fm_mapped_extents; ++i) {
    uint64_t p_offset = map->fm_extents[i].fe_physical;
    uint64_t p_len = map->fm_extents[i].fe_length;
    dsm_range.nlb = (p_len + sector_size - 1) / sector_size - 1;
    dsm_range.slba = p_offset / sector_size;
    int rc = nvme_dsm(&args);
    if (rc < 0) {
      fprintf(stderr, "nvme_dsm failed: %s\n", strerror(errno));
    }
  }

  free(args.result);
  free(map);
  close(nvme_fd);
}

void nvme_send_buffer_clean(buf_page_t *bpage) {
  nvme_send_host_hint(bpage, DSM_HINT_CLEAN);
}

void nvme_send_buffer_dirty(buf_page_t *bpage) {
  nvme_send_host_hint(bpage, DSM_HINT_DIRTY);
}

void nvme_send_buffer_evicted(buf_page_t *bpage) {
  nvme_send_host_hint(bpage, DSM_HINT_EVICTED);
}
