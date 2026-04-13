const api = require('./api');

api.listen({ port: 6767, host: '0.0.0.0' }, (err) => {
  if (err) {
    api.log.error(err)
    process.exit(1)
  }
})
