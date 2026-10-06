//! Integration tests for the P3 `spec` engine (extractor side): explicit
//! method chains, the five frozen capture kinds, the anchored disassembly
//! window, and the fail-closed row rules.

use ghostlock_extract::plugin::{ExtractContext, ExtractValue, parse_probe_tsv};
use ghostlock_extract::spec::{self, ExtractSpec, Method, SpecContext, parse_spec_row};
use std::collections::{BTreeMap, BTreeSet};

const SHA: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

#[derive(Default)]
struct Row {
    name: &'static str,
    kind: &'static str,
    required: &'static str,
    methods: &'static str,
    anchor: &'static str,
    scope: &'static str,
    pattern: &'static str,
    hit: &'static str,
    capture: &'static str,
    width: &'static str,
    signed: &'static str,
    base: &'static str,
    max_scan: &'static str,
}

fn defaults() -> Row {
    Row {
        name: "value",
        kind: "uint",
        required: "1",
        methods: "-",
        anchor: "-",
        scope: "-",
        pattern: "-",
        hit: "-",
        capture: "-",
        width: "-",
        signed: "-",
        base: "-",
        max_scan: "-",
    }
}

fn columns(row: &Row) -> String {
    [
        "spec",
        "test.schema",
        row.name,
        row.kind,
        row.required,
        row.methods,
        row.anchor,
        row.scope,
        row.pattern,
        row.hit,
        row.capture,
        row.width,
        row.signed,
        row.base,
        row.max_scan,
        "doc",
    ]
    .join("\t")
}

fn build(row: Row) -> ExtractSpec {
    let line = columns(&row);
    let parts: Vec<&str> = line.split('\t').collect();
    parse_spec_row(&parts, &line).expect("spec row parses")
}

fn build_err(row: Row) -> String {
    let line = columns(&row);
    let parts: Vec<&str> = line.split('\t').collect();
    parse_spec_row(&parts, &line)
        .expect_err("spec row must fail closed")
        .to_string()
}

fn kernel(words: &[u32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(words.len() * 4);
    for word in words {
        out.extend_from_slice(&word.to_le_bytes());
    }
    out
}

fn symbols(pairs: &[(&str, u64)]) -> BTreeMap<String, BTreeSet<u64>> {
    let mut out = BTreeMap::new();
    for (name, offset) in pairs {
        out.insert((*name).to_string(), BTreeSet::from([*offset]));
    }
    out
}

fn run(
    spec: &ExtractSpec,
    kernel: &[u8],
    raw_symbols: &BTreeMap<String, BTreeSet<u64>>,
    sorted: &[u64],
) -> spec::SpecResolution {
    let profile: BTreeMap<String, String> = BTreeMap::new();
    let paths: BTreeSet<String> = BTreeSet::new();
    let extract = ExtractContext {
        profile: &profile,
        profile_paths: &paths,
        btf: None,
        symbols: raw_symbols,
        base: 0,
    };
    let context = SpecContext {
        extract: &extract,
        kernel,
        rel_symbols: raw_symbols,
        sorted_offsets: sorted,
    };
    spec::resolve_spec(spec, &context).expect("resolution must not hard-error")
}

const PROLOGUE: u32 = 0xA9BF7BFD; /* stp x29, x30, [sp, #-16]! */
const CMP_0X10: u32 = 0xF100401F; /* cmp x0, #0x10 */
const CMP_0X20: u32 = 0xF100801F; /* cmp x0, #0x20 */
const ADRP_X1: u32 = 0xB0000001; /* adrp x1, +0x1000 */
const ADD_X1_0X10: u32 = 0x91004021; /* add x1, x1, #0x10 */
const BL_PLUS_8: u32 = 0x94000002; /* bl +8 */

#[test]
fn capture_kinds_cover_the_frozen_five() {
    let code = kernel(&[PROLOGUE, CMP_0X10, ADRP_X1, ADD_X1_0X10, BL_PLUS_8]);
    let none = BTreeMap::new();
    let empty: Vec<u64> = Vec::new();

    /* pc: the hit instruction address (cmp sits at offset 4). */
    let pc = run(
        &build(Row {
            methods: "disasm",
            anchor: "pc:0",
            scope: "text",
            pattern: "insn:cmp x?, #?",
            capture: "pc",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(pc.method, Some(Method::Disasm));
    assert_eq!(pc.value, Some(ExtractValue::UInt(4)));

    /* imm: the second predicate immediate of cmp (x0, #0x10). */
    let imm = run(
        &build(Row {
            methods: "disasm",
            anchor: "pc:0",
            scope: "text",
            pattern: "insn:cmp x?, #?",
            capture: "imm:1",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(imm.value, Some(ExtractValue::UInt(16)));

    /* reg: the first predicate register of cmp (x0 -> 0). */
    let reg = run(
        &build(Row {
            methods: "disasm",
            anchor: "pc:0",
            scope: "text",
            pattern: "insn:cmp x?, #?",
            capture: "reg:0",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(reg.value, Some(ExtractValue::UInt(0)));

    /* disp: the pre-index frame of stp is negative: int + signed + width 8. */
    let disp = run(
        &build(Row {
            kind: "int",
            methods: "disasm",
            anchor: "pc:0",
            scope: "text",
            pattern: "insn:stp x29, x30, [sp, #?]!",
            capture: "disp:2",
            width: "8",
            signed: "1",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(disp.value, Some(ExtractValue::Int(-16)));

    /* pcoff: bl at offset 0x10 with +8 resolves to image offset 0x18. */
    let pcoff = run(
        &build(Row {
            methods: "disasm",
            anchor: "pc:0",
            scope: "text",
            pattern: "insn:bl #?",
            capture: "pcoff:0",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(pcoff.value, Some(ExtractValue::UInt(0x18)));

    /* two-instruction pattern: capture the add of the adrp+add pair. */
    let paired = run(
        &build(Row {
            methods: "disasm",
            anchor: "pc:0",
            scope: "text",
            pattern: "insn:adrp x?, #?; add x?, x?, #?",
            capture: "2.imm:2",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(paired.value, Some(ExtractValue::UInt(16)));
}

#[test]
fn anchored_window_bounds_hits_and_hit_selects() {
    let mut words = vec![0u32; 0x100 / 4];
    words[0] = CMP_0X10; /* offset 0x00 */
    words[1] = CMP_0X20; /* offset 0x04 */
    words[0x40 / 4] = CMP_0X10; /* offset 0x40, beyond the next symbol */
    let code = kernel(&words);
    let raw = symbols(&[("demo_func", 0), ("higher_symbol", 0x20)]);
    let sorted = vec![0u64, 0x20];

    let first = run(
        &build(Row {
            methods: "disasm",
            anchor: "sym:demo_func",
            scope: "anchor",
            pattern: "insn:cmp x?, #?",
            capture: "imm:1",
            max_scan: "0x200",
            ..defaults()
        }),
        &code,
        &raw,
        &sorted,
    );
    assert_eq!(first.value, Some(ExtractValue::UInt(0x10)));

    let second = run(
        &build(Row {
            methods: "disasm",
            anchor: "sym:demo_func",
            scope: "anchor",
            pattern: "insn:cmp x?, #?",
            hit: "2",
            capture: "imm:1",
            max_scan: "0x200",
            ..defaults()
        }),
        &code,
        &raw,
        &sorted,
    );
    assert_eq!(second.value, Some(ExtractValue::UInt(0x20)));

    /* The next symbol bounds the window: the offset 0x40 copy is out of scope. */
    let outside = run(
        &build(Row {
            required: "0",
            methods: "disasm",
            anchor: "sym:demo_func",
            scope: "anchor",
            pattern: "insn:cmp x?, #?",
            hit: "3",
            capture: "imm:1",
            max_scan: "0x200",
            ..defaults()
        }),
        &code,
        &raw,
        &sorted,
    );
    assert!(outside.value.is_none());
    assert!(
        outside
            .reason
            .unwrap_or_default()
            .contains("hits=2 < hit=3")
    );
}

#[test]
fn explicit_chain_never_falls_back_to_the_implicit_ladder() {
    let code = kernel(&[CMP_0X10]);
    let empty: Vec<u64> = Vec::new();
    let raw = symbols(&[("init_task", 0x1000)]);

    /* A profile-only chain must NOT resolve a kernel symbol. */
    let profile_only = run(
        &build(Row {
            name: "init_task",
            required: "0",
            methods: "profile",
            ..defaults()
        }),
        &code,
        &raw,
        &empty,
    );
    assert!(profile_only.value.is_none());

    /* A kallsyms-only chain resolves it (base-relative). */
    let kallsyms_only = run(
        &build(Row {
            name: "init_task",
            methods: "kallsyms",
            ..defaults()
        }),
        &code,
        &raw,
        &empty,
    );
    assert_eq!(kallsyms_only.method, Some(Method::Kallsyms));
    assert_eq!(kallsyms_only.value, Some(ExtractValue::UInt(0x1000)));
}

#[test]
fn default_ladder_reaches_disasm_last() {
    let code = kernel(&[CMP_0X10]);
    let empty: Vec<u64> = Vec::new();
    let none = BTreeMap::new();
    let resolution = run(
        &build(Row {
            name: "not_a_symbol",
            methods: "-",
            anchor: "pc:0",
            /* scope=anchor: the default ladder carries btf/kallsyms, which the
             * frozen rules forbid together with scope=text. */
            scope: "-",
            pattern: "insn:cmp x?, #?",
            capture: "imm:1",
            ..defaults()
        }),
        &code,
        &none,
        &empty,
    );
    assert_eq!(resolution.method, Some(Method::Disasm));
    assert_eq!(resolution.value, Some(ExtractValue::UInt(16)));
}

#[test]
fn spec_rows_fail_closed() {
    let cases: Vec<(&str, Row)> = vec![
        (
            "pattern prefix",
            Row {
                pattern: "cmp x0, #1",
                ..defaults()
            },
        ),
        (
            "bytes hex",
            Row {
                pattern: "bytes:zzzz",
                ..defaults()
            },
        ),
        (
            "bytes capture",
            Row {
                pattern: "bytes:aabbccdd",
                capture: "imm:0",
                ..defaults()
            },
        ),
        (
            "insn count",
            Row {
                pattern: "insn:a; b; c; d",
                capture: "pc",
                ..defaults()
            },
        ),
        (
            "hit zero",
            Row {
                hit: "0",
                capture: "pc",
                ..defaults()
            },
        ),
        (
            "hit cap",
            Row {
                hit: "65",
                capture: "pc",
                ..defaults()
            },
        ),
        (
            "signed auto",
            Row {
                signed: "1",
                ..defaults()
            },
        ),
        (
            "int unsigned",
            Row {
                kind: "int",
                signed: "0",
                ..defaults()
            },
        ),
        (
            "uint signed",
            Row {
                kind: "uint",
                signed: "1",
                width: "4",
                ..defaults()
            },
        ),
        (
            "bool signed",
            Row {
                kind: "bool",
                signed: "1",
                width: "1",
                ..defaults()
            },
        ),
        (
            "disasm anchor",
            Row {
                methods: "disasm",
                capture: "pc",
                ..defaults()
            },
        ),
        (
            "capture without pattern",
            Row {
                capture: "imm:0",
                ..defaults()
            },
        ),
        (
            "operand index",
            Row {
                pattern: "insn:cmp x?, #?",
                capture: "imm:5",
                ..defaults()
            },
        ),
        (
            "scope text btf",
            Row {
                methods: "btf",
                scope: "text",
                ..defaults()
            },
        ),
        (
            "base anchor",
            Row {
                base: "anchor",
                ..defaults()
            },
        ),
        (
            "duplicate method",
            Row {
                methods: "profile,profile",
                ..defaults()
            },
        ),
        (
            "max scan zero",
            Row {
                max_scan: "0",
                capture: "pc",
                ..defaults()
            },
        ),
        (
            "insn index",
            Row {
                pattern: "insn:cmp x?, #?",
                capture: "2.pc",
                ..defaults()
            },
        ),
        (
            "pc operand",
            Row {
                pattern: "insn:cmp x?, #?",
                capture: "pc:0",
                ..defaults()
            },
        ),
        (
            "unknown kind",
            Row {
                pattern: "insn:cmp x?, #?",
                capture: "wat:0",
                ..defaults()
            },
        ),
    ];
    for (label, row) in cases {
        let message = build_err(row);
        assert!(!message.is_empty(), "{label} must explain the rejection");
    }
}

#[test]
fn probe_tsv_accepts_spec_rows_and_rejects_name_collisions() {
    let header = [
        "host_abi\t1",
        "countermeasures_root\tcountermeasures",
        "host_stages\tpre_spawn",
        "host_caps\tkernel_read,log",
        "stage_availability\t43499:pre_terminal;43284:post_terminal",
    ]
    .join("\n");
    /* Batch B: the probe vocabulary now carries "log"; a spec-bearing descriptor
     * must still parse (the extractor only transports the token). */
    let plugin_row =
        format!("plugin\ttest.schema\t1.0\t1\t80\t{SHA}\tpost_terminal\tkernel_read,log");
    let spec_row = "spec\ttest.schema\tvalue\tuint\t1\tdisasm\tpc:0\ttext\tinsn:cmp x?, #?\t-\timm:1\t-\t-\t-\t-\tdoc";
    let text = format!("{header}\n{plugin_row}\n{spec_row}\n");
    let descriptor = parse_probe_tsv(&text).expect("spec row parses");
    assert_eq!(descriptor.specs.len(), 1);
    assert_eq!(descriptor.specs[0].name, "value");

    let colliding = format!(
        "{header}\n{plugin_row}\nextract\ttest.schema\tvalue\tuint\t1\t0\tdoc\n{spec_row}\n"
    );
    let err =
        parse_probe_tsv(&colliding).expect_err("an extract/spec name collision rejects the module");
    assert!(err.to_string().contains("both declare"));
}
