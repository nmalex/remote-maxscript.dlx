var net = require('net');

var client = new net.Socket();
client.connect(9001, '192.168.0.150', function() {
	console.log('Connected');
	client.write('Hello, server! Love, Client.');
});

client.on('data', function(data) {
	console.log(data.toString());
});

client.on('close', function() {
	console.log('Connection closed');
});

// wait for input and send to server
var readline = require('readline');
var rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
  terminal: false
});

rl.on('line', function(line){
    line = line.trim();
    if (line === "exit") {
        client.destroy(); // kill client after server's response
        process.exit.bind(process, 0);
    }
    client.write(line);
})
