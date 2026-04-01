const api = require('./api');

api.listen({ port: 3000, host: '0.0.0.0' }, (err) => {
  if (err) {
    api.log.error(err)
    process.exit(1)
  }
})
