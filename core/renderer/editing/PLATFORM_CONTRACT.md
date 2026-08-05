# C++ to platform editing contract

This contract is the only editing-semantic boundary used by Android, Darwin,
and desktop/Clay. Platform adapters translate native APIs; they do not own a
second editing state machine.

## Ownership

The shared C++ session owns:

- UTF-16 text, forward/backward selection, composition, and revision;
- validation, input-type default actions, grapheme/atom behavior, and events;
- logical projection, hit testing, caret/selection geometry, and activation;
- whether a native operation is accepted and whether the IME must restart.

The platform adapter owns:

- connection to `InputConnection`, `UITextInput`, or the desktop text client;
- conversion between native ranges/coordinates and contract types;
- measuring currently mounted platform text/atomic views;
- platform selection UI, keyboard visibility, and synchronous IME queries.

## Threading and callbacks

All contract calls occur on the Lynx UI thread. `Snapshot()` is synchronous and
returns a value copy. C++ delegate callbacks are synchronous, non-reentrant
notifications: an adapter must post work instead of calling the session from a
callback. Platform object destruction must clear the delegate before releasing
the session.

`ProjectionSnapshot()` is also a value copy. It exposes segment/owner/range
descriptors only so a host can measure mounted content. It does not authorize a
platform adapter to rebuild projection rules or mutate the projection.

## Lifecycle

The required sequence is:

```text
attach host -> SetDelegate -> Activate -> native input active
                                     ... transactions ...
detach/focus loss -> Deactivate -> native input inactive -> SetDelegate(null)
```

Only one host in a registry may be active. Activating another host deactivates
the previous host first. `Deactivate` is idempotent. Operations against an
inactive or detached session return `kInactive` and have no side effects.

## Transactions and revisions

Every native mutation carries `expected_revision`. C++ validates the revision
before ranges and returns the current snapshot on rejection:

- `kStaleRevision`: adapter refreshes its editable/native cache;
- `kInvalidRange`: adapter drops the malformed operation;
- `kUnsupportedInputType`: frontend `beforeinput` may still handle the command;
- `kAccepted`: adapter applies the returned snapshot;
- `restart_input=true`: adapter rebuilds its native input connection after
  applying the snapshot.

Offsets are UTF-16 code units. `TextRange(base, extent)` retains direction; the
adapter must not sort selection endpoints. Composition absence is represented
by `std::nullopt`, never a magic range. The platform never mutates the rendered
tree or dispatches EditContext events directly.

## Geometry and virtualization

Coordinates are viewport-relative logical pixels. A geometry snapshot is valid
only for the exact `(state_revision, projection_revision)` pair supplied by the
host. `coverage` declares the contiguous logical range represented by `units`.

Partial coverage is explicitly valid. This is required for lazy/virtual lists:
unmounted items are missing geometry, not missing document content. Hit tests or
selection rectangles outside coverage return `kGeometryUnavailable` and invoke
`OnGeometryRequested`; the host may mount/measure the requested range and submit
a newer snapshot. No adapter may fabricate zero rectangles for unmounted text.

Each covered code unit has at most one ordered layout unit. Atomic descendants
occupy one U+FFFC unit and block boundaries one U+000A unit. Platform adapters
measure units but C++ decides atom navigation, deletion, and selection.

## Platform mappings

| Contract operation | Android | Darwin | Desktop/Clay |
| --- | --- | --- | --- |
| synchronous state | `InputConnection` queries | `UITextInput` queries | text-input client queries |
| native mutation | composing/commit/delete calls | marked text/replace calls | insert/marked text calls |
| state callback | editable + `updateSelection` | selected/marked range refresh | client snapshot refresh |
| geometry request | mounted `AndroidText`/flatten UI | mounted text/layout views | Clay text views |
| activation | focus + IME show/restart | first responder | text-input client focus |

Platform-specific code may differ in selection handles and keyboard UI, but the
contract result and shared semantic tests must be identical.
