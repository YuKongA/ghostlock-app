#include "profile/glkv3.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mpack.h>

namespace ghostlock::profile::glkv3 {
    namespace {
        DecodeCode code_from_mpack(mpack_error_t error) {
            switch (error) {
                case mpack_ok: return DecodeCode::Ok;
                case mpack_error_io:
                case mpack_error_eof: return DecodeCode::Truncated;
                case mpack_error_type:
                case mpack_error_unsupported: return DecodeCode::TypeMismatch;
                case mpack_error_too_big: return DecodeCode::TooManyEntries;
                case mpack_error_invalid:
                case mpack_error_memory:
                case mpack_error_bug:
                case mpack_error_data: return DecodeCode::Malformed;
            }
            return DecodeCode::Malformed;
        }

        struct Ctx {
            mpack_reader_t *reader = nullptr;
            DecodeCode error = DecodeCode::Ok;
            std::string_view section{};
            std::string_view key{};
        };

        struct ReaderGuard {
            mpack_reader_t *reader = nullptr;
            ~ReaderGuard() {
                if (reader != nullptr) {
                    [[maybe_unused]] const mpack_error_t destroyed =
                            mpack_reader_destroy(reader);
                }
            }
        };

        bool check(Ctx &ctx) {
            if (ctx.error != DecodeCode::Ok) return false;
            const mpack_error_t error = mpack_reader_error(ctx.reader);
            if (error != mpack_ok) {
                ctx.error = code_from_mpack(error);
                return false;
            }
            return true;
        }

        bool read_key(Ctx &ctx, std::string_view &out) {
            const uint32_t length = mpack_expect_str(ctx.reader);
            if (!check(ctx)) return false;
            const char *data =
                    mpack_read_utf8_inplace(ctx.reader, static_cast<size_t>(length));
            if (!check(ctx)) return false;
            if (data == nullptr) {
                ctx.error = DecodeCode::Malformed;
                return false;
            }
            mpack_done_str(ctx.reader);
            if (!check(ctx)) return false;
            out = std::string_view(data, static_cast<size_t>(length));
            return true;
        }

        bool read_array(Ctx &ctx, Value &out);

        bool read_scalar(Ctx &ctx, WireType expected, Value &out) {
            out = Value{};
            out.type = expected;
            switch (expected) {
                /* S4 P1: WireType::Union declares the dynamic plugin keys in the
                 * manifest; it is never a concrete wire value, so any static
                 * decode/write/convert path that meets it fails closed. */
                case WireType::Union:
                case WireType::Map:
                    return false;
                case WireType::UInt:
                    out.uint_value = mpack_expect_u64(ctx.reader);
                    return check(ctx);
                case WireType::Int:
                    out.int_value = mpack_expect_i64(ctx.reader);
                    return check(ctx);
                case WireType::Bool:
                    out.bool_value = mpack_expect_bool(ctx.reader);
                    return check(ctx);
                case WireType::Str: {
                    const uint32_t length = mpack_expect_str(ctx.reader);
                    if (!check(ctx)) return false;
                    if (length > kMaxStringBytes) {
                        ctx.error = DecodeCode::TypeMismatch;
                        return false;
                    }
                    const char *data = mpack_read_utf8_inplace(
                            ctx.reader, static_cast<size_t>(length));
                    if (!check(ctx)) return false;
                    if (data == nullptr) {
                        ctx.error = DecodeCode::Malformed;
                        return false;
                    }
                    mpack_done_str(ctx.reader);
                    if (!check(ctx)) return false;
                    out.bytes = std::string_view(data, static_cast<size_t>(length));
                    return true;
                }
                case WireType::Bin: {
                    const uint32_t length = mpack_expect_bin(ctx.reader);
                    if (!check(ctx)) return false;
                    const char *data = mpack_read_bytes_inplace(
                            ctx.reader, static_cast<size_t>(length));
                    if (!check(ctx)) return false;
                    if (data == nullptr) {
                        ctx.error = DecodeCode::Malformed;
                        return false;
                    }
                    mpack_done_bin(ctx.reader);
                    if (!check(ctx)) return false;
                    out.bytes = std::string_view(data, static_cast<size_t>(length));
                    return true;
                }
                case WireType::Array:
                    return read_array(ctx, out);
            }
            ctx.error = DecodeCode::Malformed;
            return false;
        }

        /* M2: one element map inside a declared composite. Scalar members only:
         * a nested container is a TypeMismatch instead of recursion, so the
         * representation stays bounded and depth-free. */
        bool read_map(Ctx &ctx, Value &out) {
            const uint32_t count = mpack_expect_map_max(ctx.reader, kMaxMapMembers);
            if (!check(ctx)) return false;
            out.type = WireType::Map;
            out.members.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                std::string_view key;
                if (!read_key(ctx, key)) return false;
                MapMember member;
                member.key = key;
                mpack_tag_t tag = mpack_peek_tag(ctx.reader);
                if (!check(ctx)) return false;
                Value scalar;
                switch (mpack_tag_type(&tag)) {
                    case mpack_type_uint:
                        if (!read_scalar(ctx, WireType::UInt, scalar)) return false;
                        break;
                    case mpack_type_int:
                        if (!read_scalar(ctx, WireType::Int, scalar)) return false;
                        break;
                    case mpack_type_bool:
                        if (!read_scalar(ctx, WireType::Bool, scalar)) return false;
                        break;
                    case mpack_type_str:
                        if (!read_scalar(ctx, WireType::Str, scalar)) return false;
                        break;
                    default:
                        ctx.error = DecodeCode::TypeMismatch;
                        return false;
                }
                member.type = scalar.type;
                member.uint_value = scalar.uint_value;
                member.int_value = scalar.int_value;
                member.bool_value = scalar.bool_value;
                member.bytes = scalar.bytes;
                out.members.push_back(member);
            }
            mpack_done_map(ctx.reader);
            return check(ctx);
        }
        bool read_element(Ctx &ctx, Value &out) {
            mpack_tag_t tag = mpack_peek_tag(ctx.reader);
            if (!check(ctx)) return false;
            switch (mpack_tag_type(&tag)) {
                case mpack_type_uint: return read_scalar(ctx, WireType::UInt, out);
                case mpack_type_int: return read_scalar(ctx, WireType::Int, out);
                case mpack_type_bool: return read_scalar(ctx, WireType::Bool, out);
                case mpack_type_str: return read_scalar(ctx, WireType::Str, out);
                case mpack_type_bin: return read_scalar(ctx, WireType::Bin, out);
                case mpack_type_array: return read_array(ctx, out);
                case mpack_type_map: return read_map(ctx, out);
                default:
                    ctx.error = DecodeCode::TypeMismatch;
                    return false;
            }
        }

        bool read_array(Ctx &ctx, Value &out) {
            const uint32_t count =
                    mpack_expect_array_max(ctx.reader, kMaxArrayElements);
            if (!check(ctx)) return false;
            out.type = WireType::Array;
            out.elements.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                Value element;
                if (!read_element(ctx, element)) return false;
                out.elements.push_back(std::move(element));
            }
            mpack_done_array(ctx.reader);
            return check(ctx);
        }


        bool discard_value(Ctx &ctx, unsigned depth) {
            if (depth > kMaxDepth) {
                ctx.error = DecodeCode::TooDeep;
                return false;
            }
            mpack_tag_t tag = mpack_peek_tag(ctx.reader);
            if (!check(ctx)) return false;
            const mpack_type_t type = mpack_tag_type(&tag);
            if (type != mpack_type_array && type != mpack_type_map) {
                mpack_discard(ctx.reader);
                return check(ctx);
            }
            // mpack_discard() has no depth bound; walk containers explicitly so
            // a deeply nested unknown subtree is rejected instead of skipped.
            [[maybe_unused]] const mpack_tag_t opened = mpack_read_tag(ctx.reader);
            if (!check(ctx)) return false;
            if (type == mpack_type_array) {
                const uint32_t count = mpack_tag_array_count(&tag);
                for (uint32_t i = 0; i < count; ++i) {
                    if (!discard_value(ctx, depth + 1u)) return false;
                }
                mpack_done_array(ctx.reader);
                return check(ctx);
            }
            const uint32_t count = mpack_tag_map_count(&tag);
            for (uint32_t i = 0; i < count; ++i) {
                if (!discard_value(ctx, depth + 1u)) return false;
                if (!discard_value(ctx, depth + 1u)) return false;
            }
            mpack_done_map(ctx.reader);
            return check(ctx);
        }

        /* A null schema is the neutral (schema-free) framing walk: every
         * section/key is accepted and preserved, and only the numeric wire types
         * are admitted for section values. */
        const FieldSpec *lookup(const Schema *schema, std::string_view section,
                               std::string_view key) {
            if (schema == nullptr) return nullptr;
            for (const FieldSpec &field : schema->fields) {
                if (field.section == section && field.key == key) return &field;
            }
            return nullptr;
        }

        bool section_known(const Schema *schema, std::string_view name) {
            if (schema == nullptr) return true;
            for (const FieldSpec &field : schema->fields) {
                if (!field.section.empty() && field.section == name) return true;
            }
            return false;
        }

        size_t field_index(const Schema *schema, const FieldSpec *field) {
            return static_cast<size_t>(field - schema->fields.data());
        }

        bool read_sections(Ctx &ctx, const Schema *schema, Document &staged,
                           std::vector<uint8_t> &present, DecodeMode mode) {
            const uint32_t section_count =
                    mpack_expect_map_max(ctx.reader, kMaxSections);
            if (!check(ctx)) return false;
            for (uint32_t s = 0; s < section_count; ++s) {
                std::string_view name;
                if (!read_key(ctx, name)) return false;
                ctx.section = name;
                if (!section_known(schema, name)) {
                    if (mode == DecodeMode::Production) {
                        ctx.error = DecodeCode::UnknownSection;
                        return false;
                    }
                    if (!discard_value(ctx, 2u)) return false;
                    continue;
                }
                const uint32_t entry_count =
                        mpack_expect_map_max(ctx.reader, kMaxSectionEntries);
                if (!check(ctx)) return false;
                Section &section = staged.append_section(name);
                for (uint32_t e = 0; e < entry_count; ++e) {
                    std::string_view key;
                    if (!read_key(ctx, key)) return false;
                    ctx.key = key;
                    const FieldSpec *field = lookup(schema, name, key);
                    if (field == nullptr && schema != nullptr) {
                        if (mode == DecodeMode::Production) {
                            ctx.error = DecodeCode::UnknownKey;
                            return false;
                        }
                        if (!discard_value(ctx, 3u)) return false;
                        continue;
                    }
                    Value value;
                    if (schema == nullptr) {
                        /* S4 R4 neutral framing: preserve the wire type instead
                         * of admitting only numerics, so a later owner bind can
                         * materialise String fields. Only uint/int/bool/str are
                         * admitted; bin/array still fail closed here. */
                        if (!read_element(ctx, value)) return false;
                    } else if (field->type == WireType::Array) {
                        /* M2: a declared array field (the queue) decodes as a
                         * composite; every other declared kind stays scalar. */
                        if (!read_array(ctx, value)) return false;
                        present[field_index(schema, field)] = uint8_t{1};
                    } else {
                        if (!read_scalar(ctx, field->type, value)) return false;
                        present[field_index(schema, field)] = uint8_t{1};
                    }
                    section.entries.push_back(Entry{key, std::move(value)});
                }
                mpack_done_map(ctx.reader);
                if (!check(ctx)) return false;
                ctx.key = {};
            }
            mpack_done_map(ctx.reader);
            return check(ctx);
        }
    } // namespace

    static DecodeStatus decode_impl(std::string_view input, const Schema *schema,
                                   Document &out, DecodeMode mode) {
        if (input.size() > kMaxDocumentBytes) {
            return DecodeStatus{DecodeCode::DocumentTooLarge, {}, {}};
        }
        if (input.empty()) return DecodeStatus{DecodeCode::Truncated, {}, {}};

        mpack_reader_t reader;
        mpack_reader_init_data(&reader, input.data(), input.size());
        ReaderGuard guard{&reader};
        Ctx ctx{};
        ctx.reader = &reader;

        Document staged;
        std::vector<uint8_t> present(
                schema != nullptr ? schema->fields.size() : size_t{0}, uint8_t{0});
        bool saw_schema = false;

        mpack_tag_t root = mpack_peek_tag(&reader);
        if (!check(ctx)) return DecodeStatus{ctx.error, {}, {}};
        if (mpack_tag_type(&root) != mpack_type_map) {
            return DecodeStatus{DecodeCode::NotAMap, {}, {}};
        }
        const uint32_t root_count = mpack_expect_map_max(&reader, kMaxRootEntries);
        if (!check(ctx)) return DecodeStatus{ctx.error, {}, {}};

        for (uint32_t i = 0; i < root_count; ++i) {
            std::string_view key;
            if (!read_key(ctx, key)) return DecodeStatus{ctx.error, {}, key};
            if (key == "schema") {
                Value value;
                if (!read_scalar(ctx, WireType::UInt, value)) {
                    return DecodeStatus{ctx.error, {}, key};
                }
                if (value.uint_value != kSchemaVersion) {
                    return DecodeStatus{DecodeCode::WrongSchema, {}, key};
                }
                saw_schema = true;
                staged.schema = value.uint_value;
                const FieldSpec *field = lookup(schema, std::string_view{}, key);
                if (field != nullptr) present[field_index(schema, field)] = uint8_t{1};
                continue;
            }
            if (key == "sections") {
                if (!read_sections(ctx, schema, staged, present, mode)) {
                    return DecodeStatus{ctx.error, ctx.section, ctx.key};
                }
                continue;
            }
            /* Root keys are not owner sections: the component tokens (release /
             * terminal / backend / route) and the HOCON-refactor root scalars
             * (kernel_major / kernel_minor / safe_mode) live in the document
             * root beside "schema" and "sections". The schema-free framing walk
             * has no declaration, so the key fixes the type; a schema-driven
             * walk takes the declared FieldSpec type. */
            const bool neutral = schema == nullptr;
            const bool is_token_root =
                    neutral && (key == "release" || key == "terminal" ||
                                key == "backend" || key == "route");
            const bool is_scalar_root =
                    neutral && (key == "kernel_major" || key == "kernel_minor" ||
                                key == "safe_mode");
            const FieldSpec *field = lookup(schema, std::string_view{}, key);
            if (!is_token_root && !is_scalar_root && field == nullptr) {
                if (mode == DecodeMode::Production) {
                    return DecodeStatus{DecodeCode::UnknownKey, {}, key};
                }
                if (!discard_value(ctx, 1u)) {
                    return DecodeStatus{ctx.error, {}, key};
                }
                continue;
            }
            WireType expected = WireType::Str;
            if (field != nullptr) {
                expected = field->type;
            } else if (key == "safe_mode") {
                expected = WireType::Bool;
            } else if (key == "kernel_major" || key == "kernel_minor") {
                expected = WireType::UInt;
            }
            Value value;
            if (!read_scalar(ctx, expected, value)) {
                return DecodeStatus{ctx.error, {}, key};
            }
            if (key == "release") {
                if (value.type != WireType::Str) {
                    return DecodeStatus{DecodeCode::TypeMismatch, {}, key};
                }
                staged.release = value.bytes;
                staged.has_release = true;
            } else if (key == "terminal") {
                if (value.type != WireType::Str) {
                    return DecodeStatus{DecodeCode::TypeMismatch, {}, key};
                }
                staged.terminal = value.bytes;
                staged.has_terminal = true;
            } else if (key == "backend") {
                if (value.type != WireType::Str) {
                    return DecodeStatus{DecodeCode::TypeMismatch, {}, key};
                }
                staged.backend = value.bytes;
                staged.has_backend = true;
            } else if (key == "route") {
                if (value.type != WireType::Str) {
                    return DecodeStatus{DecodeCode::TypeMismatch, {}, key};
                }
                staged.route = value.bytes;
                staged.has_route = true;
            } else if (key == "kernel_major" || key == "kernel_minor") {
                if (value.type != WireType::UInt) {
                    return DecodeStatus{DecodeCode::TypeMismatch, {}, key};
                }
                if (key == "kernel_major") {
                    staged.kernel_major = value.uint_value;
                    staged.has_kernel_major = true;
                } else {
                    staged.kernel_minor = value.uint_value;
                    staged.has_kernel_minor = true;
                }
            } else if (key == "safe_mode") {
                if (value.type != WireType::Bool) {
                    return DecodeStatus{DecodeCode::TypeMismatch, {}, key};
                }
                staged.safe_mode = value.bool_value;
                staged.has_safe_mode = true;
            } else {
                return DecodeStatus{DecodeCode::UnknownKey, {}, key};
            }
            if (field != nullptr) present[field_index(schema, field)] = uint8_t{1};
        }
        mpack_done_map(&reader);
        if (!check(ctx)) return DecodeStatus{ctx.error, {}, {}};

        if (mpack_reader_remaining(&reader, nullptr) != 0u) {
            return DecodeStatus{DecodeCode::Malformed, {}, {}};
        }
        if (!saw_schema) return DecodeStatus{DecodeCode::MissingSchema, {}, "schema"};

        if (schema != nullptr) {
            for (size_t i = 0; i < schema->fields.size(); ++i) {
                const FieldSpec &field = schema->fields[i];
                if (field.required && present[i] == 0u) {
                    return DecodeStatus{DecodeCode::MissingRequired, field.section,
                                        field.key};
                }
            }
        }

        out = std::move(staged);
        return DecodeStatus{};
    }

    DecodeStatus decode(std::string_view input, const Schema &schema, Document &out,
                        DecodeMode mode) {
        return decode_impl(input, &schema, out, mode);
    }

    DecodeStatus decode_neutral(std::string_view input, Document &out, DecodeMode mode) {
        return decode_impl(input, nullptr, out, mode);
    }

    namespace {
        void write_key(mpack_writer_t *writer, std::string_view key) {
            mpack_write_str(writer, key.data(), static_cast<uint32_t>(key.size()));
        }

        void write_text(mpack_writer_t *writer, std::string_view bytes) {
            if (bytes.size() > UINT32_MAX) {
                mpack_writer_flag_error(writer, mpack_error_too_big);
                return;
            }
            mpack_write_str(writer, bytes.data(), static_cast<uint32_t>(bytes.size()));
        }

        void write_value(mpack_writer_t *writer, const Value &value);

        void write_entries(mpack_writer_t *writer,
                           const std::vector<Entry> &entries) {
            /* Sort indices, not pointers: pointer order is nondeterministic. */
            std::vector<size_t> order(entries.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(),
                      [&entries](size_t left, size_t right) {
                          return entries[left].key < entries[right].key;
                      });
            mpack_start_map(writer, static_cast<uint32_t>(entries.size()));
            for (const size_t index : order) {
                write_key(writer, entries[index].key);
                write_value(writer, entries[index].value);
            }
            mpack_finish_map(writer);
        }

        void write_value(mpack_writer_t *writer, const Value &value) {
            switch (value.type) {
                /* S4 P1: WireType::Union declares the dynamic plugin keys in the
                 * manifest; it is never a concrete wire value, so any static
                 * decode/write/convert path that meets it fails closed. */
                case WireType::Union:
                    (void)mpack_write_nil(writer);
                    break;
                case WireType::UInt:
                    mpack_write_uint(writer, value.uint_value);
                    break;
                case WireType::Int:
                    mpack_write_int(writer, value.int_value);
                    break;
                case WireType::Bool:
                    mpack_write_bool(writer, value.bool_value);
                    break;
                case WireType::Str:
                    write_text(writer, value.bytes);
                    break;
                case WireType::Bin:
                    if (value.bytes.size() > UINT32_MAX) {
                        mpack_writer_flag_error(writer, mpack_error_too_big);
                        break;
                    }
                    mpack_write_bin(writer, value.bytes.data(),
                                    static_cast<uint32_t>(value.bytes.size()));
                    break;
                case WireType::Array:
                    mpack_start_array(writer,
                                      static_cast<uint32_t>(value.elements.size()));
                    for (const Value &element : value.elements) {
                        write_value(writer, element);
                    }
                    mpack_finish_array(writer);
                    break;
                case WireType::Map:
                    mpack_start_map(writer,
                                    static_cast<uint32_t>(value.members.size()));
                    for (const MapMember &member : value.members) {
                        write_key(writer, member.key);
                        Value scalar;
                        scalar.type = member.type;
                        scalar.uint_value = member.uint_value;
                        scalar.int_value = member.int_value;
                        scalar.bool_value = member.bool_value;
                        scalar.bytes = member.bytes;
                        write_value(writer, scalar);
                    }
                    mpack_finish_map(writer);
                    break;
            }
        }

        void write_root(mpack_writer_t *writer, const Document &document) {
            uint32_t count = 2u; /* schema + sections */
            if (document.has_backend) ++count;
            if (document.has_kernel_major) ++count;
            if (document.has_kernel_minor) ++count;
            if (document.has_release) ++count;
            if (document.has_route) ++count;
            if (document.has_safe_mode) ++count;
            if (document.has_terminal) ++count;
            mpack_start_map(writer, count);
            /* Canonical root order: UTF-8 byte sort of backend, kernel_major,
             * kernel_minor, release, route, safe_mode, schema, sections,
             * terminal. */
            if (document.has_backend) {
                write_key(writer, "backend");
                write_text(writer, document.backend);
            }
            if (document.has_kernel_major) {
                write_key(writer, "kernel_major");
                mpack_write_uint(writer, document.kernel_major);
            }
            if (document.has_kernel_minor) {
                write_key(writer, "kernel_minor");
                mpack_write_uint(writer, document.kernel_minor);
            }
            if (document.has_release) {
                write_key(writer, "release");
                write_text(writer, document.release);
            }
            if (document.has_route) {
                write_key(writer, "route");
                write_text(writer, document.route);
            }
            if (document.has_safe_mode) {
                write_key(writer, "safe_mode");
                mpack_write_bool(writer, document.safe_mode);
            }
            write_key(writer, "schema");
            mpack_write_uint(writer, document.schema);
            write_key(writer, "sections");
            mpack_start_map(writer,
                            static_cast<uint32_t>(document.sections.size()));
            std::vector<size_t> order(document.sections.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(),
                      [&document](size_t left, size_t right) {
                          return document.sections[left].name <
                                 document.sections[right].name;
                      });
            for (const size_t index : order) {
                const Section &section = document.sections[index];
                write_key(writer, section.name);
                write_entries(writer, section.entries);
            }
            mpack_finish_map(writer);
            if (document.has_terminal) {
                write_key(writer, "terminal");
                write_text(writer, document.terminal);
            }
            mpack_finish_map(writer);
        }
    } // namespace

    std::string encode(const Document &document) {
        char *data = nullptr;
        size_t size = 0;
        mpack_writer_t writer;
        mpack_writer_init_growable(&writer, &data, &size);
        write_root(&writer, document);
        const mpack_error_t error = mpack_writer_destroy(&writer);
        if (error != mpack_ok || data == nullptr) {
            if (data != nullptr) std::free(data);
            return {};
        }
        std::string encoded(data, size);
        std::free(data);
        return encoded;
    }
} // namespace ghostlock::profile::glkv3
