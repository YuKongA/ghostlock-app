/** @file jni/attack_contract_codegen.cpp
 *  从 D1 单表生成 Kotlin 参数类型与可审计字段清单。
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "jni/attack_contract_model.hpp"

namespace {
    /** @return FieldKind 的稳定 manifest 名称。 */
    [[nodiscard]] constexpr std::string_view kind_name(
        const ghostlock::jni::contract::FieldKind kind) noexcept {
        switch (kind) {
            case ghostlock::jni::contract::FieldKind::Long:
                return "long";
            case ghostlock::jni::contract::FieldKind::Int:
                return "int";
            case ghostlock::jni::contract::FieldKind::Boolean:
                return "boolean";
            case ghostlock::jni::contract::FieldKind::Bytes:
                return "bytes";
            case ghostlock::jni::contract::FieldKind::Bytes16:
                return "bytes16";
            case ghostlock::jni::contract::FieldKind::Bytes32:
                return "bytes32";
            case ghostlock::jni::contract::FieldKind::Secret32:
                return "secret32";
        }
        return "invalid";
    }

    /** @return FieldKind 对应的 Kotlin/JVM 原生字段类型。 */
    [[nodiscard]] constexpr std::string_view kotlin_type(
        const ghostlock::jni::contract::FieldKind kind) noexcept {
        switch (kind) {
            case ghostlock::jni::contract::FieldKind::Long:
                return "Long";
            case ghostlock::jni::contract::FieldKind::Int:
                return "Int";
            case ghostlock::jni::contract::FieldKind::Boolean:
                return "Boolean";
            case ghostlock::jni::contract::FieldKind::Bytes:
            case ghostlock::jni::contract::FieldKind::Bytes16:
            case ghostlock::jni::contract::FieldKind::Bytes32:
            case ghostlock::jni::contract::FieldKind::Secret32:
                return "ByteArray";
        }
        return "Nothing";
    }

    /**
     * @param path Kotlin 输出路径。
     * @return 写入成功为 true。
     */
    [[nodiscard]] bool write_kotlin(const std::filesystem::path &path) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output << "@file:Suppress(\"LongParameterList\")\n\n"
               << "package com.ghostlock.app.core.generated\n\n"
               << "/** D1 single-table field metadata. Generated; do not edit. */\n"
               << "data class AttackContractField(\n"
               << "    val name: String,\n"
               << "    val kind: String,\n"
               << "    val fixedBytes: Int,\n"
               << "    val allocationMaxBytes: Int,\n"
               << ")\n\n";

        for (const ghostlock::jni::contract::Group &group : ghostlock::jni::contract::kGroups) {
            output << "/** JNI parameter object generated from attack_contract_fields.def. */\n"
                   << "class " << group.name << "(\n";
            const std::size_t count = ghostlock::jni::contract::field_count(group.name);
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                const ghostlock::jni::contract::Field *field =
                    ghostlock::jni::contract::field_at(group.name, ordinal);
                output << "    @JvmField val " << field->name << ": " << kotlin_type(field->kind);
                output << (ordinal + 1U == count ? "\n" : ",\n");
            }
            output << ")\n\n";
        }

        output << "/** Generated layouts used by agreement tests and diagnostics. */\n"
               << "object AttackContractLayouts {\n"
               << "    val groups: Map<String, List<AttackContractField>> = linkedMapOf(\n";
        for (std::size_t group_index = 0U; group_index < ghostlock::jni::contract::kGroupCount;
             ++group_index) {
            const ghostlock::jni::contract::Group &group = ghostlock::jni::contract::kGroups[group_index];
            output << "        \"" << group.name << "\" to listOf(\n";
            const std::size_t count = ghostlock::jni::contract::field_count(group.name);
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                const ghostlock::jni::contract::Field *field =
                    ghostlock::jni::contract::field_at(group.name, ordinal);
                output << "            AttackContractField(\"" << field->name << "\", \""
                       << kind_name(field->kind) << "\", " << field->fixed_bytes << ", "
                       << field->allocation_max_bytes << "),\n";
            }
            output << "        )";
            output << (group_index + 1U == ghostlock::jni::contract::kGroupCount ? "\n" : ",\n");
        }
        output << "    )\n}\n";
        return output.good();
    }

    /**
     * @param path TSV 输出路径。
     * @return 写入成功为 true。
     */
    [[nodiscard]] bool write_manifest(const std::filesystem::path &path) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output << "group\tordinal\tfield\tkind\tjni_signature\tfixed_bytes\tallocation_max_bytes\treader\n";
        for (const ghostlock::jni::contract::Group &group : ghostlock::jni::contract::kGroups) {
            const std::size_t count = ghostlock::jni::contract::field_count(group.name);
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                const ghostlock::jni::contract::Field *field =
                    ghostlock::jni::contract::field_at(group.name, ordinal);
                output << group.name << '\t' << ordinal << '\t' << field->name << '\t'
                       << kind_name(field->kind) << '\t' << field->jni_signature << '\t'
                       << field->fixed_bytes << '\t' << field->allocation_max_bytes << "\tread"
                       << group.name
                       << '\n';
            }
        }
        return output.good();
    }
}  // namespace

/**
 * @param argc 必须为 3。
 * @param argv Kotlin 输出、TSV 输出。
 * @return 成功 0；参数或写入失败 2。
 */
int main(const int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: jni_attack_contract_codegen <kotlin-output> <manifest-output>\n";
        return 2;
    }
    const std::filesystem::path kotlin_path(argv[1]);
    const std::filesystem::path manifest_path(argv[2]);
    std::filesystem::create_directories(kotlin_path.parent_path());
    std::filesystem::create_directories(manifest_path.parent_path());
    if (!write_kotlin(kotlin_path) || !write_manifest(manifest_path)) {
        std::cerr << "jni_attack_contract_codegen: write failed\n";
        return 2;
    }
    std::cout << "jni_attack_contract_codegen: groups=" << ghostlock::jni::contract::kGroupCount
              << " fields=" << ghostlock::jni::contract::kFieldCount << '\n';
    return 0;
}
