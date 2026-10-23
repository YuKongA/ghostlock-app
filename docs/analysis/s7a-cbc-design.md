# S7a 设计：patch_file_cbc 拆分（2026-10-21）

> 从属 [split-large-functions](../plan/split-large-functions.md) S7。目标：43284 CBC 写循环拆成 socket/block/cleanup；不改 bind/connect/open/pipe 顺序。

## 状态

```c
struct cbc_io {
  int sk_send;
  int file_fd;
  int pfd[2];
};
```

栈上初始化：sk_send=file_fd=pfd[0]=pfd[1]=-1。

## 拆分

```c
static void cbc_io_init(struct cbc_io *io);
static int  cbc_setup(Glk43284State *st, const char *path, int use_helper,
                      struct cbc_io *io);
static int  cbc_block(Glk43284State *st, struct cbc_io *io, off_t off,
                      const char *payload, size_t index, int use_helper);
static void cbc_cleanup(struct cbc_io *io);
```

### cbc_setup

1. socket(AF_INET, SOCK_DGRAM, 0)
2. SO_REUSEADDR
3. bind 127.0.0.1:sender_port；失败只警告（保持）
4. connect 127.0.0.1:encap_port；失败 return -1（不 close，cleanup 唯一 owner）
5. !use_helper → open(path,O_RDONLY)；失败 return -1（同上）
6. pipe(pfd)；失败 return -1（同上）

### cbc_block

1. old_content：
   - helper → read_vendor_content(st, off, old)
   - else → pread(file_fd, old, 16, off)
2. desired = payload + index*16
3. compute_iv(st, old, desired, iv)
4. do_one_write_cbc(st, pfd[0], pfd[1], sk_send, file_fd, off, iv, helper)
5. 任一失败返回 -1

### cbc_cleanup

close pfd[0]/pfd[1]（>=0）→ close file_fd → close sk_send；
每 close 后置 -1，保证 cleanup 幂等。

### patch_file_cbc

```c
if (len % 16 != 0) return -1;
cbc_io_init;
if (cbc_setup(...) != 0) { cbc_cleanup; return -1; }
for i in len/16: if (cbc_block(...) <0) { rc=-1; break; }
cbc_cleanup;
log done/patched;
return rc;
```

## 所有权

| fd | owner | 终结 |
|---|---|---|
| sk_send | cbc_io | cleanup |
| file_fd | cbc_io | cleanup |
| pfd | cbc_io | cleanup |

setup 只返回 -1，不 close；`patch_file_cbc` 唯一调用 `cbc_cleanup`。
connect/open/pipe 失败都由 cleanup 关闭，cleanup 幂等。

## 风险

- bind 失败只警告，不 return（原行为）。
- setup 不 close；cleanup 统一 close 并置 -1。
- pread 必须 16 字节；short read 失败。
- compute_iv 使用 st->aes_key。
- `do_one_write_cbc` 的 `st->seq++` 在 24B header 构造时发生（发送前），
  保持现状；不是成功后递增。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43284.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43284.so \
  --parent patch_file_cbc \
  --helper cbc_setup --helper cbc_block --helper cbc_cleanup
# cbc_io_init 无 important 指令，不纳入展开。
# COUNT/ORDER PASS；args_diff 仅报告。
cmp_disasm --important-only：无新增 COUNT/ORDER FAIL
llvm-nm b/B=0
```
