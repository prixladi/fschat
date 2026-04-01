const Database = require('better-sqlite3')
const path = require('path')

const db = new Database(path.join(__dirname, '..', 'data.db'))

db.pragma('foreign_keys = ON')

db.exec(`
  CREATE TABLE IF NOT EXISTS channels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
  );

  CREATE TABLE IF NOT EXISTS message (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    text TEXT NOT NULL,
    username TEXT NOT NULL,
    user_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL DEFAULT (unixepoch('now', 'subsec') * 1000),
    channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE
  );
`)

module.exports = db
