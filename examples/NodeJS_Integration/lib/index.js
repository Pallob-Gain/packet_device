const {serialSystem,tcpSerialDevice,uartSerialDevice}=require('roboclaw');
const PacketDevice = require('./PacketDevice.js');
const Struct=require('./struct.V4.js');

class ProxiedStruct extends Struct {
  constructor(...args) {
    super(...args);

    return new Proxy(this, {
      get: (target, prop) => {
        if (typeof prop === "string" && target.has(prop)) return target.get(prop);
        const v = target[prop];
        return typeof v === "function" ? v.bind(target) : v;
      },
      set: (target, prop, value) => {
        if (typeof prop === "string" && target.has(prop)) {
          target.set(prop, value);
          return true;
        }
        target[prop] = value;
        return true;
      },
      has: (target, prop) => (typeof prop === "string" ? target.has(prop) : prop in target),
    });
  }
}

module.exports={PacketDevice,Struct,ProxiedStruct,serialSystem,tcpSerialDevice,uartSerialDevice};