## ADDED Requirements

### Requirement: Plugins own their Compose UI

The system SHALL load versioned KMP Compose UI entry points from verified plugin packages. The Host SHALL provide the page shell, locale, technical UI-session lifecycle, opaque state storage, preparation controller, and verified component handles, but SHALL NOT define a plugin settings schema/DSL or render backend/provider fields itself.

#### Scenario: A backend renders execution options

- **WHEN** the 43499 backend needs CPU, attempts, route, retry, or timeout options
- **THEN** its backend UI session renders and validates those controls and freezes them into its opaque backend configuration Document; Host code contains no field-specific renderer or value interpretation.

#### Scenario: A plugin UI fails to load or render

- **WHEN** its entry class, SDK compatibility, construction, or composition fails
- **THEN** the component is unavailable for the current catalog generation, no Host-generated fallback form is substituted, and no run is admitted from that UI session.

#### Scenario: UI readiness changes dynamically

- **WHEN** a plugin starts or completes preparation, changes an option, or encounters a domain failure
- **THEN** it publishes the current revision and generic `READY`, `BUSY`, `NOT_READY`, or `FAILED` status through the UI status sink; Host may gate the generic Run action but does not interpret the plugin's domain fields.

### Requirement: A backend owns placement and selection of its bound provider UI

The system SHALL expose to each backend UI only provider components that mutually match its declared parent component identity, slot, contract identity, and version range. The backend UI SHALL select and embed the provider UI; the Host SHALL only resolve, instantiate, retain, and close the verified handle.

#### Scenario: A 43499 provider is installed

- **WHEN** its parent reference and slot match the 43499 backend's corresponding provider slot
- **THEN** it is visible only in that backend's provider directory and the backend decides where its Compose `Content` appears.

#### Scenario: A provider attempts to bind to a different backend

- **WHEN** either parent identity, slot, contract identity, or accepted version fails mutual matching
- **THEN** it is absent from the directory and cannot be injected, reflected, or guessed into the other backend.

#### Scenario: A provider uses a generic extractor or toolchain

- **WHEN** a backend-bound provider declares a backend-neutral Tool dependency
- **THEN** the provider may invoke its finite job and embed optional Tool UI, then project the immutable result into its parent backend's provider snapshot; the Host does not assemble that domain pipeline.

#### Scenario: Backend and provider freeze their owned state

- **WHEN** the backend freezes execution options and the selected provider freezes physical Documents or Artifacts
- **THEN** the backend configuration Document contains only backend-owned execution configuration, while provider identity, slot/contract, revision, and physical outputs exist only in the exact provider binding; duplicate physical payload, stale revision, or mismatched binding is rejected at freeze/cutoff.

### Requirement: Host and plugin share one i18n registry

The system SHALL register Host, builtin-plugin, and imported-plugin message bundles in one KMP locale/message registry and expose plugin-owned namespaces through the UI SDK. Android resource IDs and XML strings SHALL NOT be plugin UI contracts.

#### Scenario: The application locale changes

- **WHEN** the active locale changes on Android or Desktop
- **THEN** Host and plugin Compose UI resolve their own message keys from the same locale decision and fallback engine without repackaging plugin strings into the APK.
