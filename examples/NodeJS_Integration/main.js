const {PacketDevice,Struct} = require('./lib/index.js');

const { SerialPort } = require('serialport')


const PORT = 'COM3';
const BUADRATE = 115200;

const lockInInfo=Struct.makeType({
	amplitude:Struct.type.float,
	phase:Struct.type.float
});

const packet_device = new PacketDevice('\r\n');  //delimeter passed (it only nessary for the non raw transfer, raw byte are transmit with packet info)

packet_device.onData((err,data) => {    
	if(err){
		console.log('Data error:',err);
		return;
	}
	try{
		//receive data securely
		let parsed_data=PacketDevice.dataParse(data);
		console.log('Received->',parsed_data);
		//send multiple data securly
		//packet_device.write(packet_device.dataPacket("sen",new Int32Array([1,2,34])));
	}
	catch(err){
		console.log('Data parse error:',err);
	}
});


const main = async () => {

	let list = await SerialPort.list();
	let find = list.find(item => item.path == PORT);

	if (!find) {
		console.log(`Communication port: ${PORT} is not available!`);
		return;
	}

	const serialport = new SerialPort({ path: find.path, baudRate: BUADRATE });
	
	let data_sending_thread;

	serialport.on('open', async () => {
		packet_device.open(serialport);
		console.log('Device get connected.');

		let data=new Struct(lockInInfo);
		data.setJson({
			amplitude:5.0,
			phase:0.5
		});


		packet_device.println("VNR"); //no packet data
		let version=await packet_device.waitUntillFound(['version'], 2000);
		console.log('version:',version);

		//high speed test
		for(let i=0;i<10;i++){
			packet_device.writePacket("ULX",data);
		}

		//send data 
		data_sending_thread=setInterval(()=>{
			
			packet_device.writePacket("ULX",data);

		},1000);
	});

	serialport.on('close',()=>{
		clearInterval(data_sending_thread);
		packet_device.close();
		console.log('Device get closed.');
	});

}

main();