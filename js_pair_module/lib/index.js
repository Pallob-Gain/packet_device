const {serialSystem,tcpSerialDevice,uartSerialDevice}=require('roboclaw');
const PacketDevice = require('./PacketDevice.js');
const Struct=require('./struct.V4.js');

module.exports={PacketDevice,Struct,serialSystem,tcpSerialDevice,uartSerialDevice};