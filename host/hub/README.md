# iotpp-hub

Home IoT hub for an Ubuntu server. It speaks DEX over UDP with every ESP32
sentinel on the LAN, stores monitoring data in SQLite and serves a web UI
(REST + Server-Sent Events) for status, configuration, actions, real-time
charts and notifications. Single self-contained binary: the web UI and its
libraries are compiled in.

```sh
run hub demo                 # Hub + 3 simulated ESP32s → http://localhost:8080
run hub start                # Hub only (database in host/data/)
run hub dev                  # Serve host/hub/web from disk: edit + reload, no rebuild
run hub sim --devices 5      # Simulator only (127.0.0.10..14 → hub on 127.0.0.1:4444)
iotpp-hub --help             # All options (ports, DB, polling, retention, --device ...)
```

In the devcontainer, ports `8080/tcp` (web) and `4444/udp` (DEX) are published;
rebuild the container after pulling this change. Configure the ESP32s'
`CONFIG_MONITOR_HOST` with the hub machine's LAN IP. On a server, install the
binary and [deploy/iotpp-hub.service](deploy/iotpp-hub.service).

## Design: DEX is the single source of truth

```text
libs/dex/*.hpp  ──dex_describe()/dex_flags()/Peripheral traits──┐
                                                                 ▼
        reflect.hpp (generic walk + magic_enum) ──► registry (codecs, signals, /api/schema)
                                                                 ▼
  dex_link (UDP) ──► devices (poll, execute, ingest, diff → events) ──► storage (SQLite)
                                                                 ▼
                     http_api (REST + SSE) ──► web UI (schema-driven Preact + uPlot)
```

Each DEX payload struct is described **once**, next to its definition, with
units, scale, ranges and docs ([dex_meta.hpp](../../libs/dex/dex_meta.hpp)).
The hub derives everything else from those descriptions at compile time:

| Concern | Generated from DEX |
|---|---|
| Binary ⇄ JSON (engineering units, enum names) | `reflect::to_json` / `reflect::parse` |
| Validation of writes (ranges, enum values, sizes) | `parse` + field `range()` / numeric limits |
| Peripheral-specific limits on shared sub-structs | `meta::at("path").range(..)` / `.readonly()` |
| Cross-field rules, shared with the firmware | traits `static bool valid(const Config &)` |
| Monitoring signals (flattened leaves, labels, units) | `collect_signals` / `collect_values` |
| Web forms, status views, action buttons, charts | `/api/schema` consumed by the UI |
| Enum/flag names | `magic_enum` (flags detected via `dex_flags`) |
| Completeness | `static_assert`: a description must cover `sizeof(T)` |

Nothing in the hub or the web UI names a concrete peripheral.

### Adding a peripheral or a signal

1. Define the packed structs in a `libs/dex/dex_<name>.hpp` header as usual.
2. Describe each struct (`dex_describe`) and add a `Peripheral` traits struct:

   ```cpp
   constexpr auto dex_describe(const Sample *)
   {
     using Self = Sample;
     return meta::fields(DEX_FIELD(temperature).unit("°C").scale(0.01),
                         DEX_FIELD(humidity).unit("%").range(0, 100));
   }
   struct Peripheral
   {
     static constexpr std::uint8_t type = Type;
     static constexpr const char *name = "Bulbasaur";
     static constexpr const char *description = "Climate sensor";
     using Status = StatusData;            // derives from dex::StatusData
     using Config = ConfigData;            // or dex::meta::None
     using Action = ActionData;            // or dex::meta::None
     using Monitoring = MonitoringPublication; // or dex::meta::None
     static bool valid(const Config &c) { return valid_config(c); } // optional
   };
   ```

   Tighten or freeze fields inherited from shared structs by path, and keep
   the limits as constants the firmware uses too:

   ```cpp
   constexpr auto dex_describe(const ConfigData *)
   {
     return meta::fields(meta::base<peripheral::ConfigData>(),
                         meta::at("monitoring.samplePeriodMs").range(MinSamplePeriodMs, MaxSamplePeriodMs),
                         meta::at("monitoring").readonly());   // whole subtree
   }
   ```

   The firmware validates with the same `valid_config()`; the hub checks
   ranges, read-only fields and `valid()` before sending, and the UI shows
   the limits and disables read-only inputs.
3. Append it to `dex::Peripherals` in [dex.hpp](../../libs/dex/dex.hpp) and rebuild.

The web UI then shows its status, a config form with validation, action
buttons (one per enum value when the action is a single enum), live tiles and
charts for every signal, and the protocol reference — without UI changes.
Adding a field to an existing struct only needs one `DEX_FIELD(...)` line; a
forgotten one is a compile error.

## Notifications

Every poll (default 5 s) reads device and peripheral status. Rising
`Notification` bits become **notification** events (pulsing toast, screen-edge
flash, ringing bell, chime, tab counter, desktop notification when hidden).
Rising `Error` bits are **errors**, cleared ones **success**, `Fault` /
`Warning` / `Degraded` states **warnings**, connectivity changes and discrete
signal changes (e.g. door Open/Closed) **info**. Events are stored and can be
filtered and acknowledged.

## API

| Method | Path | |
|---|---|---|
| GET | `/api/schema` | Full DEX description (commands, enums, types, signals) |
| GET/POST | `/api/devices` | List / add `{host, port, name}` |
| GET/PATCH/DELETE | `/api/devices/:key` | Detail / rename / forget |
| POST | `/api/devices/:key/command` | Any DEX command `{command, peripheral?, data?}` |
| POST | `/api/devices/:key/raw` | `{command, payload: "hex"}` |
| POST | `/api/devices/:key/refresh`, `/api/discover` | Re-read everything / broadcast Info |
| GET | `/api/signals`, `/api/history?ids=&from=&to=&points=` | Catalog / downsampled history |
| GET/POST | `/api/events`, `/api/events/counts`, `/api/events/ack` | Notifications |
| GET | `/api/stream` | SSE: `device`, `samples`, `event`, `ack`, `removed` |
| GET | `/api/stats` | Counters |

Config writes are applied onto the last values read from the device, so a
partial object such as `{"monitoring": {"publishPeriodMs": 5000}}` is enough.
Read-only fields may be repeated unchanged but not modified.

## Transport

The DEX port (4444) socket receives only unsolicited traffic: monitoring
publications, pushed status and discovery replies. Every request opens its
own ephemeral socket connected to the device, so a late reply to a timed-out
request can never be mistaken for the answer to a later one (DEX replies have
no sequence number). Exchanges with one device are serialized; polling runs
on at most `--poll-workers` (default 8) concurrent workers.

## Storage

SQLite (WAL). Samples live in a narrow `samples(signal, ts, value)` table
(`WITHOUT ROWID`, primary key `(signal, ts)`), so new signals need no schema
change. History queries are downsampled server-side (avg/min/max per bucket)
to the chart width. `--retention DAYS` (default 90) prunes old samples and
acknowledged events hourly.

## Layout

```text
host/hub/main.cpp           options, wiring, signals
host/hub/src/reflect.hpp    generic DEX reflection (JSON, schema, signals)
host/hub/src/registry.*     peripheral types from dex::Peripherals, /api/schema
host/hub/src/dex_link.*     UDP transport: listener + one socket per exchange
host/hub/src/devices.*      inventory, polling, commands, ingestion, events
host/hub/src/storage.*      SQLite
host/hub/src/http_api.*     REST, SSE, static files
host/hub/web/               UI (ES modules, no build step; embedded at build time)
host/sim/                   ESP32 simulator for development and demos
```
