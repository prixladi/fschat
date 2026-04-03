const db = require('./db');
const api = require('fastify')({ logger: true });

api.register(async (fastify) => {
  fastify.get('/channels', (_, reply) => {
    const result = db.prepare('SELECT * FROM channels').all();
    return reply.code(200).send({ result });
  });

  fastify.post('/channels', (req, reply) => {
    const { name } = req.body;
    const channel = db
      .prepare('SELECT id FROM channels WHERE name = ?')
      .get(name);

    if (channel)
      return reply
        .code(409)
        .send({ message: 'channel with provided id already exists' });

    const result = db
      .prepare('INSERT INTO channels (name) VALUES (?)')
      .run(name);
    return reply.code(201).send({ id: result.lastInsertRowid, name });
  });

  fastify.delete('/channels/:id', (req, reply) => {
    const result = db
      .prepare('DELETE FROM channels WHERE id = ?')
      .run(req.params.id);
    if (result.changes === 0)
      return reply.code(404).send({ error: 'Channel not found' });

    return reply.code(204).send();
  });

  fastify.post('/channels/:id/messages', (req, reply) => {
    const channelId = Number(req.params.id);
    const channel = db
      .prepare('SELECT id FROM channels WHERE id = ?')
      .get(channelId);
    if (!channel) return reply.code(404).send({ error: 'Channel not found' });

    const { text, username, user_id } = req.body;
    const timestamp = Date.now();
    const result = db
      .prepare(
        'INSERT INTO messages (text, timestamp, username, user_id, channel_id) VALUES (?, ?, ?, ?, ?)',
      )
      .run(text, timestamp, username, user_id, channelId);

    return reply.code(201).send({ id: result.lastInsertRowid });
  });

  fastify.get('/channels/:id/messages', (req, reply) => {
    const channelId = Number(req.params.id);
    const channel = db
      .prepare('SELECT id FROM channels WHERE id = ?')
      .get(channelId);
    if (!channel) return reply.code(404).send({ error: 'Channel not found' });

    const { since } = req.query;
    const params = [channelId];
    let sql = 'SELECT * FROM messages WHERE channel_id = ?';

    if (since) {
      sql += ' AND timestamp >= ?';
      params.push(Number(since));
    }

    sql += ' ORDER BY timestamp ASC';

    return db.prepare(sql).all(params);
  });
});

module.exports = api;
