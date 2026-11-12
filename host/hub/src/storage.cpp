#include "storage.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>

namespace hub
{
namespace
{
// Minimal RAII prepared statement with positional binding.
class Statement
{
public:
  Statement(sqlite3 *db, const char *sql) : db_(db)
  {
    if(sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
      throw std::runtime_error(std::string("SQL prepare: ") + sqlite3_errmsg(db) + " in " + sql);
  }
  ~Statement() { sqlite3_finalize(stmt_); }
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;

  template <typename... A>
  Statement &bind(const A &...values)
  {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    int index = 0;
    (bind_one(++index, values), ...);
    return *this;
  }
  bool step()
  {
    const auto rc = sqlite3_step(stmt_);
    if(rc == SQLITE_ROW)
      return true;
    if(rc != SQLITE_DONE)
      throw std::runtime_error(std::string("SQL step: ") + sqlite3_errmsg(db_));
    return false;
  }
  void run()
  {
    step();
    sqlite3_reset(stmt_);
  }
  std::int64_t integer(int column) const { return sqlite3_column_int64(stmt_, column); }
  double real(int column) const { return sqlite3_column_double(stmt_, column); }
  bool null(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }
  std::string text(int column) const
  {
    const auto *value = sqlite3_column_text(stmt_, column);
    return value ? reinterpret_cast<const char *>(value) : "";
  }

private:
  void bind_one(int index, std::int64_t value) { sqlite3_bind_int64(stmt_, index, value); }
  void bind_one(int index, int value) { sqlite3_bind_int64(stmt_, index, value); }
  void bind_one(int index, double value) { sqlite3_bind_double(stmt_, index, value); }
  void bind_one(int index, const std::string &value)
  {
    sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }

  sqlite3 *db_;
  sqlite3_stmt *stmt_ = nullptr;
};

Event read_event(const Statement &s)
{
  return {s.integer(0), s.integer(1), s.text(2), static_cast<int>(s.integer(3)),
          s.text(4),    s.text(5),    s.text(6), s.integer(7) != 0};
}
} // namespace

json Event::to_json() const
{
  json out = {{"id", id}, {"ts", ts}, {"device", device}, {"severity", severity},
              {"title", title}, {"detail", detail}, {"acked", acked}};
  out["peripheral"] = peripheral >= 0 ? json(peripheral) : json();
  return out;
}

Storage::Storage(const std::string &path)
{
  if(sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("Cannot open database " + path + ": " + sqlite3_errmsg(db_));
  sqlite3_busy_timeout(db_, 5000);
  exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
  exec(R"sql(
    CREATE TABLE IF NOT EXISTS devices(
      key TEXT PRIMARY KEY, host TEXT NOT NULL, port INTEGER NOT NULL, name TEXT NOT NULL DEFAULT '');
    CREATE TABLE IF NOT EXISTS signals(
      id INTEGER PRIMARY KEY, device TEXT NOT NULL, peripheral INTEGER NOT NULL,
      ptype INTEGER NOT NULL, key TEXT NOT NULL, UNIQUE(device, peripheral, key));
    CREATE TABLE IF NOT EXISTS samples(
      signal INTEGER NOT NULL, ts INTEGER NOT NULL, value REAL,
      PRIMARY KEY(signal, ts)) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS events(
      id INTEGER PRIMARY KEY, ts INTEGER NOT NULL, device TEXT NOT NULL, peripheral INTEGER NOT NULL,
      severity TEXT NOT NULL, title TEXT NOT NULL, detail TEXT NOT NULL, acked INTEGER NOT NULL DEFAULT 0);
    CREATE INDEX IF NOT EXISTS events_ts ON events(ts);
  )sql");
}

Storage::~Storage() { sqlite3_close(db_); }

void Storage::exec(const char *sql)
{
  char *error = nullptr;
  if(sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK)
  {
    const std::string message = error ? error : "unknown";
    sqlite3_free(error);
    throw std::runtime_error("SQL: " + message);
  }
}

std::int64_t Storage::signal_id(const std::string &device, int peripheral, int type, const std::string &key)
{
  std::lock_guard lock(mutex_);
  const auto cacheKey = std::make_tuple(device, peripheral, key);
  if(const auto it = signalCache_.find(cacheKey); it != signalCache_.end())
    return it->second;
  Statement insert(db_, "INSERT INTO signals(device, peripheral, ptype, key) VALUES(?,?,?,?) "
                        "ON CONFLICT(device, peripheral, key) DO UPDATE SET ptype=excluded.ptype");
  insert.bind(device, peripheral, type, key).run();
  Statement select(db_, "SELECT id FROM signals WHERE device=? AND peripheral=? AND key=?");
  select.bind(device, peripheral, key);
  if(!select.step())
    throw std::runtime_error("Cannot register signal " + key);
  return signalCache_[cacheKey] = select.integer(0);
}

void Storage::insert(const std::vector<Sample> &samples)
{
  if(samples.empty())
    return;
  std::lock_guard lock(mutex_);
  exec("BEGIN");
  try
  {
    Statement insert(db_, "INSERT OR REPLACE INTO samples(signal, ts, value) VALUES(?,?,?)");
    for(const auto &sample : samples)
      insert.bind(sample.signal, sample.ts, sample.value).run();
    exec("COMMIT");
  }
  catch(...)
  {
    exec("ROLLBACK");
    throw;
  }
}

json Storage::history(const std::vector<std::int64_t> &ids, std::int64_t from, std::int64_t to, int points)
{
  std::lock_guard lock(mutex_);
  points = std::clamp(points, 10, 20000);
  // Server-side downsampling keeps responses small for any time range.
  const std::int64_t bucket = std::max<std::int64_t>(1, (to - from) / points);
  const bool raw = bucket <= 1000;
  Statement rows(db_, "SELECT ts, value, value, value FROM samples WHERE signal=? AND ts BETWEEN ? AND ? ORDER BY ts");
  Statement grouped(db_, "SELECT (ts / ?1) * ?1 AS t, avg(value), min(value), max(value) FROM samples "
                         "WHERE signal=?2 AND ts BETWEEN ?3 AND ?4 GROUP BY t ORDER BY t");
  json series = json::array();
  for(const auto id : ids)
  {
    auto &query = raw ? rows : grouped;
    if(raw)
      rows.bind(id, from, to);
    else
      grouped.bind(bucket, id, from, to);
    json t = json::array(), v = json::array(), lo = json::array(), hi = json::array();
    while(query.step())
    {
      t.push_back(query.integer(0));
      v.push_back(query.real(1));
      if(!raw)
        lo.push_back(query.real(2)), hi.push_back(query.real(3));
    }
    json entry = {{"id", id}, {"t", std::move(t)}, {"v", std::move(v)}};
    if(!raw)
      entry["min"] = std::move(lo), entry["max"] = std::move(hi);
    series.push_back(std::move(entry));
  }
  return {{"from", from}, {"to", to}, {"bucket", raw ? 0 : bucket}, {"series", std::move(series)}};
}

json Storage::signals(const std::string &device)
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "SELECT s.id, s.device, s.peripheral, s.ptype, s.key, l.ts, l.value FROM signals s "
                   "LEFT JOIN samples l ON l.signal = s.id AND l.ts = (SELECT max(ts) FROM samples WHERE signal = s.id) "
                   "WHERE ?1 = '' OR s.device = ?1 ORDER BY s.device, s.peripheral, s.id");
  s.bind(device);
  json out = json::array();
  while(s.step())
  {
    json row = {{"id", s.integer(0)}, {"device", s.text(1)}, {"peripheral", s.integer(2)},
                {"type", s.integer(3)}, {"key", s.text(4)}};
    row["ts"] = s.null(5) ? json() : json(s.integer(5));
    row["value"] = s.null(6) ? json() : json(s.real(6));
    out.push_back(std::move(row));
  }
  return out;
}

std::int64_t Storage::add_event(Event &event)
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "INSERT INTO events(ts, device, peripheral, severity, title, detail, acked) VALUES(?,?,?,?,?,?,0)");
  s.bind(event.ts, event.device, event.peripheral, event.severity, event.title, event.detail).run();
  return event.id = sqlite3_last_insert_rowid(db_);
}

json Storage::events(int limit, std::int64_t beforeId, const std::string &severity, const std::string &device,
                     bool unackedOnly)
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "SELECT id, ts, device, peripheral, severity, title, detail, acked FROM events "
                   "WHERE (?1 = 0 OR id < ?1) AND (?2 = '' OR severity = ?2) AND (?3 = '' OR device = ?3) "
                   "AND (?4 = 0 OR acked = 0) ORDER BY id DESC LIMIT ?5");
  s.bind(beforeId, severity, device, unackedOnly ? 1 : 0, std::clamp(limit, 1, 1000));
  json out = json::array();
  while(s.step())
    out.push_back(read_event(s).to_json());
  return out;
}

int Storage::ack(const std::vector<std::int64_t> &ids)
{
  std::lock_guard lock(mutex_);
  if(ids.empty())
    exec("UPDATE events SET acked = 1 WHERE acked = 0");
  else
  {
    Statement s(db_, "UPDATE events SET acked = 1 WHERE id = ?");
    for(const auto id : ids)
      s.bind(id).run();
  }
  return sqlite3_changes(db_);
}

json Storage::event_counts()
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "SELECT severity, count(*) FROM events WHERE acked = 0 GROUP BY severity");
  json out = json::object();
  while(s.step())
    out[s.text(0)] = s.integer(1);
  return out;
}

void Storage::save_device(const DeviceRow &device)
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "INSERT INTO devices(key, host, port, name) VALUES(?,?,?,?) "
                   "ON CONFLICT(key) DO UPDATE SET host=excluded.host, port=excluded.port, name=excluded.name");
  s.bind(device.key, device.host, device.port, device.name).run();
}

void Storage::remove_device(const std::string &key)
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "DELETE FROM devices WHERE key = ?");
  s.bind(key).run();
}

std::vector<DeviceRow> Storage::devices()
{
  std::lock_guard lock(mutex_);
  Statement s(db_, "SELECT key, host, port, name FROM devices ORDER BY key");
  std::vector<DeviceRow> out;
  while(s.step())
    out.push_back({s.text(0), s.text(1), static_cast<int>(s.integer(2)), s.text(3)});
  return out;
}

void Storage::prune(std::int64_t olderThanMs)
{
  std::lock_guard lock(mutex_);
  Statement samples(db_, "DELETE FROM samples WHERE ts < ?");
  samples.bind(olderThanMs).run();
  Statement events(db_, "DELETE FROM events WHERE ts < ? AND acked = 1");
  events.bind(olderThanMs).run();
}

json Storage::stats()
{
  std::lock_guard lock(mutex_);
  const auto count = [&](const char *sql) {
    Statement s(db_, sql);
    return s.step() ? s.integer(0) : 0;
  };
  return {{"samples", count("SELECT count(*) FROM samples")},
          {"signals", count("SELECT count(*) FROM signals")},
          {"events", count("SELECT count(*) FROM events")},
          {"dbBytes", count("SELECT page_count * page_size FROM pragma_page_count(), pragma_page_size()")}};
}
} // namespace hub
