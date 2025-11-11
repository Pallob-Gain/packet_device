const { PacketDevice, Struct } = require('./lib/index.js');

const { SerialPort } = require('serialport')


const PORT = process.env.PORT?.trim() || 'COM6';
const BUADRATE = 115200;

const MassiveData = Struct.makeType({
	name: Struct.stringType(100),
	other_data: Struct.arrayType(Struct.type.float, 200)
});

const lockInInfo = Struct.makeType({
	amplitude: Struct.type.float,
	phase: Struct.type.float
});

const Bio = Struct.makeType({
	age: Struct.type.uint8_t,
	height: Struct.type.uint8_t
});



const packet_device = new PacketDevice('\r\n');  //delimeter passed (it only nessary for the non raw transfer, raw byte are transmit with packet info)

packet_device.onData((err, data) => {
	if (err) {
		console.log('Data error:', err);
		return;
	}
	try {
		//receive data securely
		//console.log('Raw data length:', data.length);
		let parsed_data = PacketDevice.dataParse(data);

		if ('mdt' in parsed_data) {
			const data_struct = new Struct(MassiveData);
			data_struct.collect(parsed_data['mdt']);
			console.log('Massive data name:', data_struct.getJson().name);
		}
		else console.log('Received->', parsed_data);
		//send multiple data securly
		//packet_device.write(packet_device.dataPacket("sen",new Int32Array([1,2,34])));
	}
	catch (err) {
		console.log('Data parse error:', err);
	}
});


const main = async () => {

	let list = await SerialPort.list();
	let find = list.find(item => item.path == PORT);

	if (!find) {
		console.log(`Communication port: ${PORT} is not available at: `, list.map(i => i.path));
		return;
	}

	const serialport = new SerialPort({ path: find.path, baudRate: BUADRATE });

	let data_sending_thread;

	serialport.on('open', async () => {
		packet_device.open(serialport);
		console.log('Device get connected.');

		let data = new Struct(lockInInfo);
		data.setJson({
			amplitude: 5.0,
			phase: 0.5
		});

		let counts = new Array(10).fill(0).map(() => Math.random() * 100);

		console.log('Sending Counts:', counts);

		packet_device.println("VNR"); //no packet data
		let version = await packet_device.waitUntillFound(['version'], 2000);
		console.log('version:', version);

		//high speed test
		for (let i = 0; i < 10; i++) {
			packet_device.writePacket("ULX", data);
		}

		//send data 
		data_sending_thread = setInterval(() => {

			packet_device.writePacket("ULX", data);

			packet_device.writePacket("counts", new Float32Array(counts));

			const bios=new Array(5).fill(null).map((_,idx)=>{
				const item = new Struct(Bio);
				item.setJson({age:20+idx,height:150+idx*2});
				return item;
			});
			//console.log(bios.map(i=>i.getJson()));
			packet_device.writePacket("bio", bios);

		}, 1000);
	});

	serialport.on('close', () => {
		clearInterval(data_sending_thread);
		packet_device.close();
		console.log('Device get closed.');
	});

}

main();