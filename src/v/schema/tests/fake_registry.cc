/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#include "schema/tests/fake_registry.h"

#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,cert-err58-cpp)
static ss::logger dummy_logger("schema_test_logger");

namespace ppsr = pandaproxy::schema_registry;

} // namespace

ss::future<ppsr::subject_schema> schema::fake_store::get_subject_schema(
  ppsr::subject sub,
  std::optional<ppsr::schema_version> version,
  ppsr::include_deleted) {
    std::optional<ppsr::subject_schema> found;
    for (const auto& s : schemas) {
        if (s.schema.sub() != sub) {
            continue;
        }
        if (version && *version != s.version) {
            continue;
        }
        if (found && found->version > s.version) {
            continue;
        }
        found.emplace(s.share());
    }
    co_return std::move(found).value();
}

ss::future<ppsr::canonical_schema_definition>
schema::fake_store::get_schema_definition(ppsr::schema_id id) {
    for (const auto& s : schemas) {
        if (s.id == id) {
            co_return s.schema.def().share();
        }
    }
    throw std::runtime_error("unknown schema id");
}

void schema::fake_registry::maybe_throw_injected_failure() const {
    if (_injected_failure) {
        std::rethrow_exception(_injected_failure);
    }
}

ss::future<ppsr::canonical_schema_definition>
schema::fake_registry::get_schema_definition(ppsr::schema_id id) const {
    maybe_throw_injected_failure();
    return _store.get_schema_definition(id);
}
ss::future<ppsr::subject_schema> schema::fake_registry::get_subject_schema(
  ppsr::subject sub, std::optional<ppsr::schema_version> version) const {
    maybe_throw_injected_failure();
    return _store.get_subject_schema(sub, version, ppsr::include_deleted::no);
}
ss::future<ppsr::schema_getter*> schema::fake_registry::getter() const {
    maybe_throw_injected_failure();
    co_return &_store;
}
ss::future<ppsr::schema_id>
schema::fake_registry::create_schema(ppsr::unparsed_schema unparsed) {
    maybe_throw_injected_failure();
    // This is wrong, but simple for our testing.
    for (const auto& s : _store.schemas) {
        if (s.schema.def().raw()() == unparsed.def().raw()()) {
            co_return s.id;
        }
    }
    auto version = ppsr::schema_version(0);
    for (const auto& s : _store.schemas) {
        if (s.schema.sub() == unparsed.sub()) {
            version = std::max(version, s.version);
        }
    }
    // TODO: validate references too
    auto [sub, unparsed_def] = std::move(unparsed).destructure();
    auto [def, type, refs] = std::move(unparsed_def).destructure();
    _store.schemas.push_back({
      .schema = ppsr::canonical_schema(
        std::move(sub),
        ppsr::canonical_schema_definition(
          ppsr::canonical_schema_definition::raw_string{std::move(def)()},
          type,
          std::move(refs))),
      .version = version + 1,
      .id = ppsr::schema_id(int32_t(_store.schemas.size() + 1)),
      .deleted = ppsr::is_deleted::no,
    });
    co_return _store.schemas.back().id;
}
const std::vector<ppsr::subject_schema>& schema::fake_registry::get_all() {
    maybe_throw_injected_failure();
    return _store.schemas;
}
