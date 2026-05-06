import express from 'express';
import cors from 'cors';
import grpc from '@grpc/grpc-js';
import protoLoader from '@grpc/proto-loader';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROTO_PATH = path.resolve(__dirname, '../proto/kvstore.proto');

const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true
});

const kvstoreProto = grpc.loadPackageDefinition(packageDefinition).kvstore;
const client = new kvstoreProto.KVStore('localhost:50051', grpc.credentials.createInsecure());

const app = express();
app.use(cors());
app.use(express.json());

app.get('/api/status', (req, res) => {
  client.Status({}, (error, response) => {
    if (error) return res.status(500).json({ error: error.message });
    res.json(response);
  });
});

app.get('/api/kv/:key', (req, res) => {
  client.Get({ key: req.params.key }, (error, response) => {
    if (error) return res.status(500).json({ error: error.message });
    res.json(response);
  });
});

app.post('/api/kv', (req, res) => {
  const { key, value } = req.body;
  if (!key || !value) return res.status(400).json({ error: 'Key and value are required' });
  client.Put({ key, value }, (error, response) => {
    if (error) return res.status(500).json({ error: error.message });
    res.json(response);
  });
});

app.delete('/api/kv/:key', (req, res) => {
  client.Delete({ key: req.params.key }, (error, response) => {
    if (error) return res.status(500).json({ error: error.message });
    res.json(response);
  });
});

const PORT = 3001;
app.listen(PORT, () => {
  console.log(`KVStore API Gateway listening on port ${PORT}`);
});
