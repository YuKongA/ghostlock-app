/* S4 P1 plugin probe implementation (contract-design 3.14.7.1 / 3.14.7.2).
 *
 * Read-only by construction: the module is opened, glk_entry() is called, the
 * static description is copied to the TSV and the handle is closed. Nothing is
 * registered, no hook pointer is ever called, and no file descriptor or mapping
 * outlives the call. The hash decision happens before dlopen. */

#include "plugin/probe.hpp"

#include "plugin/schema.hpp"

#include "contract/countermeasure.hpp"
#include "support/sha256.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace ghostlock::plugin {
    namespace {
        /* Size of the v1 glk_module (everything before the P1 tail append). A
         * module reporting less is rejected; the tail is read only when size
         * reaches GLK_MODULE_SIZE_V2. */
        constexpr std::uint32_t kModuleSizeV1 =
                static_cast<std::uint32_t>(offsetof(glk_module, param_count));
        constexpr std::uint32_t kMaxParams = 32u;

        void emit(const ProbeSink &sink, const std::string &line) noexcept {
            if (sink.write != nullptr) {
                sink.write(sink.ctx, line);
            }
        }

        void emit_error(const ProbeSink &err, std::string_view text) noexcept {
            if (err.write != nullptr) {
                err.write(err.ctx, text);
            }
        }

        std::string_view null_dash(const char *text) noexcept {
            if (text == nullptr || text[0] == '\0') {
                return std::string_view("-");
            }
            return std::string_view(text);
        }

        bool valid_hex64(const char *hex) noexcept {
            if (hex == nullptr) {
                return false;
            }
            std::size_t length = 0u;
            for (; hex[length] != '\0'; ++length) {
                const char ch = hex[length];
                const bool lower = ch >= 'a' && ch <= 'f';
                const bool digit = ch >= '0' && ch <= '9';
                if (!lower && !digit) {
                    return false;
                }
            }
            return length == support::kSha256HexLength;
        }

        std::string reject_line(std::string_view id, LoadStatus reason) {
            std::string line = "reject\t";
            line += id.empty() ? std::string_view("-") : id;
            line += '\t';
            line += load_status_name(reason);
            return line;
        }

        int fail(const ProbeSink &out, const ProbeSink &err, std::string_view id,
                 LoadStatus reason) {
            emit(out, reject_line(id, reason));
            std::string text = "plugin_probe: ";
            text += load_status_name(reason);
            emit_error(err, text);
            return 1;
        }

        /* Comma-joined stage tokens of a bitmask; empty mask -> "-". */
        std::string stage_list(std::uint32_t mask) {
            std::string out;
            for (const contract::CountermeasureStage stage :
                 {contract::CountermeasureStage::PreSpawn,
                  contract::CountermeasureStage::PostSpawn,
                  contract::CountermeasureStage::PreTerminal,
                  contract::CountermeasureStage::PreRoute,
                  contract::CountermeasureStage::PostTerminal}) {
                if ((mask & stage_bit(stage)) == 0u) {
                    continue;
                }
                if (!out.empty()) {
                    out += ',';
                }
                out += contract::stage_token(stage);
            }
            return out.empty() ? std::string("-") : out;
        }

        std::string caps_list(std::uint32_t mask) {
            std::string out;
            for (const contract::Capability capability :
                 {contract::Capability::KernelRead, contract::Capability::KernelWrite,
                  contract::Capability::Alias, contract::Capability::ChildTask,
                  contract::Capability::FileCacheWrite, contract::Capability::Exec,
                  contract::Capability::KernelHook}) {
                const auto bit = static_cast<std::uint32_t>(capability);
                if ((mask & bit) == 0u) {
                    continue;
                }
                if (!out.empty()) {
                    out += ',';
                }
                out += contract::capability_token(capability);
            }
            return out.empty() ? std::string("-") : out;
        }

        /* Audit D7/D8: names are part of the TSV and of the loader-visible
         * descriptor, so the probe mirrors the registration checks that can be
         * seen here: no control characters, and bounded length. A name that
         * would be rejected at registration must not be published as if valid. */
        bool name_has_control(std::string_view name) noexcept {
            for (const char ch : name) {
                const unsigned char raw = static_cast<unsigned char>(ch);
                if (raw < 0x20u || raw == 0x7fu) return true;
            }
            return false;
        }

        bool param_name_valid(const glk_param &param) noexcept {
            if (param.name == nullptr) return false;
            const std::string_view name(param.name);
            if (name.empty() || name.size() > kMaxHookNameLength) return false;
            return !name_has_control(name);
        }

        std::string_view param_type_token(std::uint32_t type) noexcept {
            switch (type) {
                case GLK_PARAM_UINT: return "uint";
                case GLK_PARAM_INT: return "int";
                case GLK_PARAM_BOOL: return "bool";
                case GLK_PARAM_STR: return "str";
                default: return "unknown";
            }
        }

        std::string param_row(std::string_view kind, std::string_view id,
                              const glk_param &param) {
            std::string line(kind);
            line += '\t';
            line += id;
            line += '\t';
            line += null_dash(param.name);
            line += '\t';
            line += param_type_token(param.type);
            line += '\t';
            line += param.required != 0u ? "1" : "0";
            line += '\t';
            if (param.type == GLK_PARAM_STR) {
                line += null_dash(param.default_str);
            } else if (param.type == GLK_PARAM_BOOL) {
                line += param.default_value != 0u ? "1" : "0";
            } else if (param.type == GLK_PARAM_INT) {
                /* Signed type: a negative default must print signed, or every
                 * consumer rejects the whole descriptor (audit D1). */
                line += std::to_string(static_cast<std::int64_t>(param.default_value));
            } else {
                line += std::to_string(param.default_value);
            }
            line += '\t';
            line += null_dash(param.doc);
            return line;
        }

        /* Frozen header value (Lead ruling 2026-10-05): the RELATIVE directory
         * name, not an absolute path -- the probe process and the App do not
         * necessarily share GHOSTLOCK_HOME, so only the relative name is
         * comparable. "-" means the probe has no home and nothing to check. */
        std::string countermeasures_root() {
            const char *home = std::getenv("GHOSTLOCK_HOME");
            if (home == nullptr || home[0] == '\0') {
                return std::string("-");
            }
            return std::string(kCountermeasuresDirName);
        }
    } // namespace

    int probe_plugin(std::string_view path, const char *expect_sha256,
                     const LoaderOps &ops, const ProbeSink &out,
                     const ProbeSink &err) noexcept {
        if (path.empty()) {
            return fail(out, err, {}, LoadStatus::InvalidArgument);
        }
        const std::string owned_path(path);

        /* Hash first: an --expect-sha256 mismatch must never reach dlopen. */
        char digest[support::kSha256HexLength + 1u] = {};
        const auto hash_fn =
                ops.sha256_file != nullptr ? ops.sha256_file : &support::sha256_file;
        if (hash_fn(owned_path.c_str(), digest, sizeof(digest)) != 0) {
            return fail(out, err, {}, LoadStatus::HashRejected);
        }
        if (expect_sha256 != nullptr && expect_sha256[0] != '\0') {
            if (!valid_hex64(expect_sha256)) {
                return fail(out, err, {}, LoadStatus::InvalidArgument);
            }
            if (std::strcmp(expect_sha256, digest) != 0) {
                return fail(out, err, {}, LoadStatus::HashMismatch);
            }
        }

        if (ops.open_lib == nullptr || ops.sym == nullptr || ops.close_lib == nullptr) {
            return fail(out, err, {}, LoadStatus::InvalidArgument);
        }
        void *handle = ops.open_lib(owned_path.c_str());
        if (handle == nullptr) {
            return fail(out, err, {}, LoadStatus::OpenFailed);
        }

        int result = 0;
        {
            auto *entry = reinterpret_cast<EntryFn>(ops.sym(handle, "glk_entry"));
            if (entry == nullptr) {
                ops.close_lib(handle);
                return fail(out, err, {}, LoadStatus::EntryMissing);
            }
            const glk_module *module = entry(GLK_ABI_VERSION);
            if (module == nullptr) {
                ops.close_lib(handle);
                return fail(out, err, {}, LoadStatus::AbiMismatch);
            }
            /* Owned copy: every tail-failure path closes the library before
             * reporting, so the id must not alias the module image. */
            const std::string id(null_dash(module->name));
            if (module->abi_version != GLK_ABI_VERSION) {
                ops.close_lib(handle);
                return fail(out, err, id, LoadStatus::AbiMismatch);
            }
            if (module->size < kModuleSizeV1) {
                ops.close_lib(handle);
                return fail(out, err, id, LoadStatus::SizeIncompatible);
            }
            if (module->name == nullptr || module->name[0] == '\0' ||
                std::strlen(module->name) > kMaxModuleNameLength) {
                ops.close_lib(handle);
                return fail(out, err, id, LoadStatus::NameInvalid);
            }
            if (module->version == nullptr ||
                std::strlen(module->version) > kMaxModuleVersionLength) {
                ops.close_lib(handle);
                return fail(out, err, id, LoadStatus::VersionInvalid);
            }

            const bool tail = module->size >= GLK_MODULE_SIZE_V2;
            const std::uint32_t param_count = tail ? module->param_count : 0u;
            const glk_param *params = tail ? module->params : nullptr;
            const std::uint32_t extract_count = tail ? module->extract_count : 0u;
            const glk_param *extract = tail ? module->extract : nullptr;
            std::uint32_t stage_mask = tail ? module->stage_mask : 0u;

            std::string rejects;
            bool caps_rejected = false;
            bool trigger_rejected = false;
            bool stage_rejected = false;
            bool hook_bounds_rejected = false;

            const std::uint32_t host_caps =
                    static_cast<std::uint32_t>(contract::kHostImplementedCaps);
            if ((module->required_caps & ~host_caps) != 0u) {
                caps_rejected = true;
            }
            if (module->hook_count > kMaxHooks ||
                (module->hook_count != 0u && module->hooks == nullptr)) {
                hook_bounds_rejected = true;
            }

            emit(out, std::string("host_abi\t") + std::to_string(GLK_ABI_VERSION));
            emit(out, std::string("countermeasures_root\t") + countermeasures_root());
            emit(out, std::string("host_stages\t") + stage_list(kHostImplementedStages));
            emit(out, std::string("host_caps\t") + caps_list(host_caps));
            /* Frozen P1 revision: the per-backend stage availability matrix, so
             * the App can grey out stages without hard-coding a second table. */
            /* Audit D5: a backend with no available stage would produce an empty
             * group ("43499:"), which both consumers reject. The invariant is
             * asserted in schema.hpp; here an empty group is simply omitted and
             * an all-empty line would fall back to "-". */
            std::string availability = "stage_availability\t";
            bool first_backend = true;
            for (const RuntimeBackend backend :
                 {RuntimeBackend::Cve2026_43499, RuntimeBackend::Cve2026_43284}) {
                std::string group;
                for (const contract::CountermeasureStage stage :
                     {contract::CountermeasureStage::PreSpawn,
                      contract::CountermeasureStage::PostSpawn,
                      contract::CountermeasureStage::PreTerminal,
                      contract::CountermeasureStage::PostTerminal}) {
                    if (!stage_available_on(backend, stage)) continue;
                    if (!group.empty()) group += ',';
                    group += contract::stage_token(stage);
                }
                if (group.empty()) continue;
                if (!first_backend) availability += ';';
                availability += backend_short_token(backend);
                availability += ':';
                availability += group;
                first_backend = false;
            }
            if (first_backend) availability += '-';
            emit(out, availability);

            std::vector<std::string> hook_lines;
            const std::uint32_t hook_count =
                    module->hook_count > kMaxHooks ? kMaxHooks : module->hook_count;
            for (std::uint32_t i = 0u; i < hook_count && module->hooks != nullptr; ++i) {
                const glk_hook &hook = module->hooks[i];
                stage_mask |= stage_bit(static_cast<contract::CountermeasureStage>(
                        static_cast<std::uint32_t>(hook.stage)));
                if (hook.trigger != GLK_TRIGGER_ON_STAGE) {
                    trigger_rejected = true;
                }
                if ((kHostImplementedStages &
                     stage_bit(static_cast<contract::CountermeasureStage>(
                             static_cast<std::uint32_t>(hook.stage)))) == 0u) {
                    stage_rejected = true;
                }
                /* Audit D4: a hook row whose required columns are missing must NOT
                 * be emitted -- consumers reject the malformed row before they ever
                 * see the reject line, which would hide the real reason. Required:
                 * fn, name (non-null), and a name the loader would accept. */
                if (hook.fn == nullptr || hook.name == nullptr ||
                    hook.name[0] == '\0' ||
                    std::strlen(hook.name) > kMaxHookNameLength ||
                    name_has_control(std::string_view(hook.name))) {
                    hook_bounds_rejected = true;
                    continue;
                }
                std::string line = "hook\t";
                line += id;
                line += '\t';
                line += contract::trigger_token(
                        static_cast<contract::CountermeasureTrigger>(
                                static_cast<std::uint32_t>(hook.trigger)));
                line += '\t';
                line += contract::stage_token(
                        static_cast<contract::CountermeasureStage>(
                                static_cast<std::uint32_t>(hook.stage)));
                line += '\t';
                line += std::to_string(hook.priority);
                line += '\t';
                line += null_dash(hook.name);
                hook_lines.push_back(std::move(line));
            }

            if ((stage_mask & ~kHostImplementedStages) != 0u) {
                stage_rejected = true;
            }

            std::string plugin_line = "plugin\t";
            plugin_line += id;
            plugin_line += '\t';
            plugin_line += null_dash(module->version);
            plugin_line += '\t';
            plugin_line += std::to_string(module->abi_version);
            plugin_line += '\t';
            plugin_line += std::to_string(module->size);
            plugin_line += '\t';
            plugin_line += digest;
            plugin_line += '\t';
            plugin_line += stage_list(stage_mask);
            plugin_line += '\t';
            plugin_line += caps_list(module->required_caps);
            emit(out, plugin_line);
            for (const std::string &hook_line : hook_lines) {
                emit(out, hook_line);
            }
            if (param_count > kMaxParams ||
                (param_count != 0u && params == nullptr)) {
                rejects += reject_line(id, LoadStatus::InvalidArgument);
                rejects += '\n';
            } else {
                for (std::uint32_t i = 0u; i < param_count; ++i) {
                    if (params[i].type > GLK_PARAM_STR ||
                        !param_name_valid(params[i])) {
                        rejects += reject_line(id, LoadStatus::InvalidArgument);
                        rejects += '\n';
                        continue; /* never publish a row the loader would reject */
                    }
                    emit(out, param_row("param", id, params[i]));
                }
            }
            if (extract_count > kMaxParams ||
                (extract_count != 0u && extract == nullptr)) {
                rejects += reject_line(id, LoadStatus::InvalidArgument);
                rejects += '\n';
            } else {
                for (std::uint32_t i = 0u; i < extract_count; ++i) {
                    if (extract[i].type > GLK_PARAM_STR ||
                        !param_name_valid(extract[i])) {
                        rejects += reject_line(id, LoadStatus::InvalidArgument);
                        rejects += '\n';
                        continue;
                    }
                    emit(out, param_row("extract", id, extract[i]));
                }
            }

            if (caps_rejected) {
                rejects += reject_line(id, LoadStatus::CapsRejected);
                rejects += '\n';
            }
            if (trigger_rejected) {
                rejects += reject_line(id, LoadStatus::TriggerRejected);
                rejects += '\n';
            }
            if (stage_rejected) {
                rejects += reject_line(id, LoadStatus::StageRejected);
                rejects += '\n';
            }
            if (hook_bounds_rejected) {
                rejects += reject_line(id, LoadStatus::HooksMissing);
                rejects += '\n';
            }
            std::size_t start = 0u;
            while (start < rejects.size()) {
                const std::size_t end = rejects.find('\n', start);
                if (end == std::string::npos) {
                    break;
                }
                emit(out, rejects.substr(start, end - start));
                start = end + 1u;
            }
        }
        ops.close_lib(handle);
        return result;
    }

    int run_plugin_probe(std::string_view path, const char *expect_sha256) noexcept {
#if defined(__linux__)
        /* The probe never needs new privileges: drop them before dlopen. */
        (void)::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif
        const ProbeSink out{[](void *, std::string_view line) {
                                (void)std::fwrite(line.data(), 1u, line.size(), stdout);
                                (void)std::fputc('\n', stdout);
                            },
                            nullptr};
        const ProbeSink err{[](void *, std::string_view line) {
                                (void)std::fwrite(line.data(), 1u, line.size(), stderr);
                                (void)std::fputc('\n', stderr);
                            },
                            nullptr};
        return probe_plugin(path, expect_sha256, default_loader_ops(), out, err);
    }
} // namespace ghostlock::plugin
