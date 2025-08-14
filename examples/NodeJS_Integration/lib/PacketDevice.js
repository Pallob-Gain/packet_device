const Struct = require('./struct.V4.js');
const DataEndPusherExtractor = require('./DataEndPusherExtractor.V3.js');

const TRANSFER_DATA_BUFFER_SIG = 0x2A;

const BUFFER_TEXT_RESPNOSE = 0x5E;
const BUFFER_PARAM_RESPNOSE = 0x5F;
const BUFFER_ARRY_RESPNOSE = 0x60;

const BUFFER_JSON_RESPONSE_START = 0x7B;
const BUFFER_JSON_RESPONSE_END = 0x7D;

const DATA_TYPE_UINT64_T = 1;
const DATA_TYPE_INT64_T = 2;
const DATA_TYPE_UINT32_T = 3;
const DATA_TYPE_INT32_T = 4;
const DATA_TYPE_UINT16_T = 5;
const DATA_TYPE_INT16_T = 6;
const DATA_TYPE_UINT8_T = 7;
const DATA_TYPE_INT8_T = 8;
const DATA_TYPE_INT = 9;
const DATA_TYPE_UINT = 10;
const DATA_TYPE_FLOAT = 11;
const DATA_TYPE_DOUBLE = 12;
const DATA_TYPE_LONG = 13;
const DATA_TYPE_ULONG = 14;
const DATA_TYPE_STRING = 15;
const DATA_TYPE_BOOL = 16;
const DATA_TYPE_NULL= 17;
const DATA_TYPE_VOID = 0;

const STRUCT_EQUVALENT_TYPE = {
    uint64_t: DATA_TYPE_UINT64_T,
    int64_t: DATA_TYPE_INT64_T,
    uint32_t: DATA_TYPE_UINT32_T,
    int32_t: DATA_TYPE_INT32_T,
    uint16_t: DATA_TYPE_UINT16_T,
    int16_t: DATA_TYPE_INT16_T,
    uint8_t: DATA_TYPE_UINT8_T,
    int8_t: DATA_TYPE_INT8_T,
    int: DATA_TYPE_INT,
    uint: DATA_TYPE_UINT,
    float: DATA_TYPE_FLOAT,
    double: DATA_TYPE_DOUBLE,
    long: DATA_TYPE_LONG,
    ulong: DATA_TYPE_ULONG,
    string: DATA_TYPE_STRING,
    bool: DATA_TYPE_BOOL,
    null: DATA_TYPE_NULL,
    void: DATA_TYPE_VOID
};

// Mapping JavaScript Typed Arrays to Type IDs
const TYPED_ARRAY_MAP = new Map([
    [BigUint64Array, DATA_TYPE_UINT64_T],
    [BigInt64Array, DATA_TYPE_INT64_T],
    [Uint32Array, DATA_TYPE_UINT32_T],
    [Int32Array, DATA_TYPE_INT32_T],
    [Uint16Array, DATA_TYPE_UINT16_T],
    [Int16Array, DATA_TYPE_INT16_T],
    [Uint8Array, DATA_TYPE_UINT8_T],
    [Int8Array, DATA_TYPE_INT8_T],
    [Float32Array, DATA_TYPE_FLOAT],
    [Float64Array, DATA_TYPE_DOUBLE],
    [Uint8ClampedArray, DATA_TYPE_UINT8_T], // Clamped version of Uint8Array
]);

// Mapping JavaScript Primitive Types to Type IDs
const TYPE_MAP = new Map([
    ["bigint", [DATA_TYPE_UINT64_T, DATA_TYPE_INT64_T]], // BigInt types
    ["number", [DATA_TYPE_UINT32_T, DATA_TYPE_INT32_T, DATA_TYPE_UINT16_T, DATA_TYPE_INT16_T, DATA_TYPE_UINT8_T, DATA_TYPE_INT8_T, DATA_TYPE_INT, DATA_TYPE_UINT, DATA_TYPE_FLOAT, DATA_TYPE_DOUBLE, DATA_TYPE_LONG, DATA_TYPE_ULONG]], // Various number types
    ["string", [DATA_TYPE_STRING]], // Strings
    ["boolean", [DATA_TYPE_BOOL]], // Booleans
    ["object", [DATA_TYPE_NULL]], // Null is an object in JS
    ["undefined", [DATA_TYPE_VOID]] // Undefined/Void
]);

const TYPE_SIZE_MAP = new Map([
    [DATA_TYPE_UINT64_T, 8], // BigUint64 (8 bytes)
    [DATA_TYPE_INT64_T, 8], // BigInt64 (8 bytes)
    [DATA_TYPE_UINT32_T, 4], // Uint32 (4 bytes)
    [DATA_TYPE_INT32_T, 4], // Int32 (4 bytes)
    [DATA_TYPE_UINT16_T, 2], // Uint16 (2 bytes)
    [DATA_TYPE_INT16_T, 2], // Int16 (2 bytes)
    [DATA_TYPE_UINT8_T, 1], // Uint8 (1 byte)
    [DATA_TYPE_INT8_T, 1], // Int8 (1 byte)
    [DATA_TYPE_FLOAT, 4], // Float32 (4 bytes)
    [DATA_TYPE_DOUBLE, 8], // Float64 (8 bytes)
    [DATA_TYPE_LONG, 4], // Assuming 4 bytes for long (platform-dependent)
    [DATA_TYPE_ULONG, 4], // Assuming 4 bytes for unsigned long (platform-dependent)
    [DATA_TYPE_STRING, null], // Strings are variable-sized
    [DATA_TYPE_BOOL, 1], // Boolean (stored as 1 byte)
    [DATA_TYPE_NULL, 0], // Null has no size
    [DATA_TYPE_VOID, 1] // Void has no size
]);

const type_conversion = {
    [DATA_TYPE_UINT64_T]: (buff, size) => buff.readUInt64LE(0),
    [DATA_TYPE_INT64_T]: (buff, size) => buff.readInt64LE(0),
    [DATA_TYPE_UINT32_T]: (buff, size) => buff.readUInt32LE(0),
    [DATA_TYPE_INT32_T]: (buff, size) => buff.readInt32LE(0),
    [DATA_TYPE_UINT16_T]: (buff, size) => buff.readUInt16LE(0),
    [DATA_TYPE_INT16_T]: (buff, size) => buff.readInt16LE(0),
    [DATA_TYPE_UINT8_T]: (buff, size) => buff.readUInt8(0),
    [DATA_TYPE_INT8_T]: (buff, size) => buff.readInt8(0),
    [DATA_TYPE_INT]: (buff, size) => size == 2 ? buff.readInt16LE(0) : buff.readInt32LE(0),
    [DATA_TYPE_UINT]: (buff, size) => size == 2 ? buff.readUInt16LE(0) : buff.readUInt32LE(0),
    [DATA_TYPE_FLOAT]: (buff, size) => buff.readFloatLE(0),
    [DATA_TYPE_DOUBLE]: (buff, size) => buff.readDoubleLE(0),
    [DATA_TYPE_LONG]: (buff, size) => buff.readBigInt64LE(0),
    [DATA_TYPE_ULONG]: (buff, size) => buff.readBigUInt64LE(0),
    [DATA_TYPE_STRING]: (buff, size) => buff.toString(),
    [DATA_TYPE_BOOL]: (buff, size) => buff.readUInt8(0)!=0,
    [DATA_TYPE_NULL]: (buff, size) =>null,
    [DATA_TYPE_VOID]: (buff, size) => buff.readUInt8(0),
};

// Function to get the type size
const getTypeSize=(typeId)=> {
    return TYPE_SIZE_MAP.has(typeId) ? TYPE_SIZE_MAP.get(typeId) : null;
}

const getTypeIdExtended=(value)=> {
    if (value === null) return DATA_TYPE_NULL; // Null case
    if (value === undefined) return DATA_TYPE_VOID; // Undefined case

    // Check for Typed Arrays
    for (const [typedArray, typeId] of TYPED_ARRAY_MAP) {
        if (value instanceof typedArray) return typeId;
    }

    // Normal JavaScript types
    const jsType = typeof value;
    return TYPE_MAP.has(jsType) ? TYPE_MAP.get(jsType)[0] : null;
}

const BufferTextResponseHeader=Struct.makeType({
    signeture: Struct.type.byte,
    signeture_type: Struct.type.byte,
    data_size: Struct.type.byte,
});

const BufferParamResponseHeader= Struct.makeType({
    signeture: Struct.type.byte,
    signeture_type: Struct.type.byte,
    data_type: Struct.type.byte,
    pram_len: Struct.type.byte,
    data_size: Struct.type.byte,
});

const BufferArrayResponseHeader = Struct.makeType({
    signeture: Struct.type.byte,
    signeture_type: Struct.type.byte,
    data_type: Struct.type.byte,
    type_size: Struct.type.byte,
    pram_len: Struct.type.byte,
    data_size: Struct.type.byte,
});

const buffer_response_parsing = {
    [BUFFER_TEXT_RESPNOSE]: {
        parse: (buff) => {
            //buff_signeture(1 byte)+data_signeture(1 byte)+data_len(1 bytes)+data_buff(data_len bytes)
            if (buff.length < 3) throw new Error('Invalid data length!');
            let data_len = buff[2];
            if (buff.length < data_len + 3) throw new Error('Data is not sufficient in length!');
            return buff.subarray(3, 3 + data_len).toString();
        }
    },
    [BUFFER_PARAM_RESPNOSE]: {
        parse: (buff) => {
            //buff_signeture(1 byte)+data_signeture(1 byte)+string_type(1 bytes)+pram_len(1 bytes)+data_len(1 bytes)+prams_buff(pram_len bytes)+data_buff(data_len bytes)
            if (buff.length < 5) throw new Error('Invalid data length!');
            let data_type = buff[2];
            let pram_len = buff[3];
            let data_len = buff[4];

            if (buff.length < pram_len + data_len + 5) throw new Error('Data is not sufficient in length!');

            let [prams_buff, data_buff] = [{ start: 5, len: pram_len }, { start: 5 + pram_len, len: data_len }].map(({ start, len }) => buff.subarray(start, start + len));

            return { [prams_buff.toString()]: type_conversion[data_type](data_buff,data_len) };
        }
    },
    [BUFFER_ARRY_RESPNOSE]: {
        parse: (buff) => {
            //buff_signeture(1 byte)+data_signeture(1 byte)+type(1 bytes)+type_size(1 bytes)+pram_len(1 bytes)+data_size(1 bytes)+prams_buff+data_buff
            //console.log('Array data parsing');

            if (buff.length < 6) throw new Error('Invalid data length!');
            let type = buff[2];

            //console.log('Type:',type);

            if (!(type in type_conversion)) throw new Error('Invalid data type!');

            let type_conv = type_conversion[type];

            let type_size = buff[3];
            let pram_len = buff[4];
            let data_size = buff[5];
            let data_len = type_size * data_size;

            //console.log('Info:',{type_size,pram_len,data_len,data_size,data_len});

            if (buff.length < pram_len + data_len + 6) throw new Error('Data is not sufficient in length!');

            let prams_buff = buff.subarray(6, 6 + pram_len);
            let data_pos_to = 6 + pram_len + data_len;
            let data_array = [];
            for (let i = 6 + pram_len; i < data_pos_to; i += type_size) {
                let data_buff = buff.subarray(i, i + type_size);
                data_array.push(type_conv(data_buff, type_size));
            }

            //console.log('data:',prams_buff.toString(),data_array);

            return { [prams_buff.toString()]: data_array };
        }
    }
};


const buffer_response_maker={
    [BUFFER_TEXT_RESPNOSE]:(data)=>{
        let data_holder = new Struct(BufferTextResponseHeader);
        data_holder.set('signeture',TRANSFER_DATA_BUFFER_SIG);
        data_holder.set('signeture_type',BUFFER_TEXT_RESPNOSE);
        data_holder.set('data_size',data.length);

        return Buffer.concat([
            data_holder.ref(),
            Buffer.from(data)
        ]);
    },
    [BUFFER_PARAM_RESPNOSE]:(param,data,force_type=null)=>{
        let data_type=force_type===null?getTypeIdExtended(data):force_type;
        
        if(data_type===null)throw new Error('Data is not valid');

        let data_holder = new Struct(BufferParamResponseHeader);
        data_holder.set('signeture',TRANSFER_DATA_BUFFER_SIG);
        data_holder.set('signeture_type',BUFFER_PARAM_RESPNOSE);
        data_holder.set('data_type',data_type);
        data_holder.set('pram_len',param.length);
        data_holder.set('data_size',data.length);

        return Buffer.concat([
            data_holder.ref(),
            Buffer.from(param),
            Buffer.from(data.buffer)
        ]);
    },
    [BUFFER_ARRY_RESPNOSE]:(param,data,typed_array=true)=>{
        if(typed_array){
            let data_type=getTypeIdExtended(data);
            let type_size=getTypeSize(data_type);
            
            if(data_type===null || type_size===null)throw new Error('Data is not valid');

            let data_holder = new Struct(BufferArrayResponseHeader);
            data_holder.set('signeture',TRANSFER_DATA_BUFFER_SIG);
            data_holder.set('signeture_type',BUFFER_ARRY_RESPNOSE);
            data_holder.set('data_type',data_type);
            data_holder.set('type_size',type_size);
            data_holder.set('pram_len',param.length);
            data_holder.set('data_size',data.length);

            return Buffer.concat([
                data_holder.ref(),
                Buffer.from(param),
                Buffer.from(data.buffer)
            ]);
        }
        else{
            //console.log('Array Struct Transfer');

            if(!(Array.isArray(data) && data.length>0 && typeof data[0]=='object' && data[0] instanceof Struct))throw new Error('Data is not valid');
            if(data.length==0)throw new Error('Data is empty.');

            let data_type=DATA_TYPE_VOID; //struct array transfer data type is void
            let type_size=data[0].size();

            if(!data.every(row=>typeof row=='object' && row instanceof Struct && row.size()==type_size))throw new Error('Data array is invalid.');

            let data_holder = new Struct(BufferArrayResponseHeader);
            data_holder.set('signeture',TRANSFER_DATA_BUFFER_SIG);
            data_holder.set('signeture_type',BUFFER_ARRY_RESPNOSE);
            data_holder.set('data_type',data_type);
            data_holder.set('type_size',type_size);
            data_holder.set('pram_len',param.length);
            data_holder.set('data_size',data.length);

            let trans_data=Buffer.concat(data.map(row=>row.ref()));

            //console.log('type_size:',type_size);
            //console.log('data_type:',data_type);
            //console.log('pram_len:',param.length);
            //console.log('data_size',data.length);
            //console.log('trans_data:',trans_data.length,trans_data);

            return Buffer.concat([
                data_holder.ref(),
                Buffer.from(param),
                trans_data
            ]);
        }
    },
};

module.exports = class PacketDevice {
    attached_serial_dev;
    delimiter='';
    onDataCb = [];
    dataReceiverHolder = [];

    static Type=Object.fromEntries(Object.entries(Struct.type).map(([name,type])=>{
        return [name,{
            type:STRUCT_EQUVALENT_TYPE[name],
            details:Struct.makeType({value:type})
        }];
    }));

    constructor(delimiter='\r\n') {
        this.delimiter=delimiter;
        this.dataParser = new DataEndPusherExtractor(delimiter);
    }

    open(serial_dev){
        this.attached_serial_dev=serial_dev;
        serial_dev.on('data',(buff)=>{
            
            //console.log('data:',buff,'len:',buff.length);

            this.dataReceiver(buff);
        });
    }

    close(){
        this.attached_serial_dev=null; //release device
    }

    write(buffer){
        if(this.attached_serial_dev && 'write' in this.attached_serial_dev){
            return this.attached_serial_dev.write(buffer);
        }
        else throw new Error('Device is not opened!');
    }

    writePacket(param,data){
        let packet=this.dataPacket(param,data,false);
        if(packet===null)throw new Error('Invalid data!');
        return this.write(packet);
    }

    println(data){
        return this.write(Buffer.concat([Buffer.from(data), Buffer.from(this.delimiter)]));
    }
    
    static getTypedValue(type,value){
        //console.log(type,typeof type);
        if(typeof type=='object' && 'details' in type){
            let data_holder = new Struct(type.details);
            data_holder.set('value',value);
            return {
                type:type.type,
                value:data_holder
            };
        }
        else throw new Error('Invalid type!');
    }

    static getDataCrc(buff){
        //CRC-16 (CCITT-FALSE) 
        let crc = 0;
        for (let i = 0; i < buff.length; i++) {
            let val = buff[i] & 0xFF;
            crc = crc ^ (val << 8);
            for (let j = 0; j < 8; j++) {
                if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
                else crc = crc << 1;
            }
            crc = crc & 0xFFFF;
        }

        return crc & 0xFFFF;
    }

    static checkCrcValidity(buff) {
        let len = buff.length;
        if (len <= 2) return null;
        
        let data=buff.subarray(0, len - 2);

        let crc=PacketDevice.getDataCrc(data);
        if(crc===null)return null;

        let crc_msb = buff[len - 2] & 0xFF;
        let crc_lsb = buff[len - 1] & 0xFF;

        let check_crc = (crc_msb << 8) | crc_lsb;

        let status = check_crc == crc ?  data: null;
        // if(!status){
        //     let s=buff.toString();
        //     console.log(`data:`,buff,s,s.length);
        //     console.log(`check_crc: ${check_crc}, crc: ${crc}`);
        // }

        return status;
    }

    static jsonParse(buff) {
        return JSON.parse(buff.toString());
    }

    static dataParse(buff) {
        //console.log('data parsing:',buff);
        //console.log('data first:',buff[0],buff[buff.length-1]);

        if (buff[0] == BUFFER_JSON_RESPONSE_START && buff[buff.length - 1] == BUFFER_JSON_RESPONSE_END) return PacketDevice.jsonParse(buff);
        else if (buff[0] == TRANSFER_DATA_BUFFER_SIG && buff.length > 2 && buff[1] in buffer_response_parsing) {
            return buffer_response_parsing[buff[1]].parse(buff);
        }

        throw new Error('The received data is not parseable!');
    }

    static bufferGenerate(param,data){
        if(data!==null){
            //console.log(param,typeof data=='object',data instanceof Struct);
            if(ArrayBuffer.isView(data)){
                //typed array
                return buffer_response_maker[BUFFER_ARRY_RESPNOSE](param,data);
            }
            else if(Array.isArray(data) && data.length>0 && typeof data[0]=='object' && data[0] instanceof Struct){
                //Structed array
                return buffer_response_maker[BUFFER_ARRY_RESPNOSE](param,data,false);
            }
            else if(typeof data=='object' && 'value' in data && data.value instanceof Struct){
                //value type, a single typed data send 
                return buffer_response_maker[BUFFER_PARAM_RESPNOSE](param,data.value.ref(),data.type);
            }
            else if(typeof data=='object' && data instanceof Struct){
                //value type: a complete struct send
                return buffer_response_maker[BUFFER_PARAM_RESPNOSE](param,data.ref(),DATA_TYPE_VOID);
            }
            else throw new Error('Data is invalid!');
        }
        else if(typeof param=='string'){
            //text type
            return buffer_response_maker[BUFFER_TEXT_RESPNOSE](param);
        }
        return null;
    }

    dataPacket(param,data,ending=false){
        let buff=PacketDevice.bufferGenerate(param,data);
        if(buff===null)return null;
        let crc=PacketDevice.getDataCrc(buff);
       
        if(ending){
            //end with delimeter
            return Buffer.concat([
                buff,
                Buffer.from([
                    crc>>8 & 0xFF,
                    crc & 0xFF,
                ]),
                Buffer.from(this.delimiter)
            ]);
        }
        else{
            //transfer with buffer
            return Buffer.concat([
                this.dataParser.updatePacketLength(buff.length+2),
                buff,
                Buffer.from([
                    crc>>8 & 0xFF,
                    crc & 0xFF,
                ])
            ]);
        }
    }

    clearBufferQueue() {
        this.dataParser.clear();
    }

    checkDeviceData(buff_data) {
        let buff_packets = this.dataParser.pushOut(buff_data);
        if (buff_packets==null || buff_packets.length == 0) return null;

        //console.log(buff_packets);

        return buff_packets.map(buff => {
            return PacketDevice.checkCrcValidity(buff);
        }).filter(t => t);
    }

    dataReceiveHandel(err, data) {
        while (this.dataReceiverHolder.length > 0) {
            let callback = this.dataReceiverHolder.shift();
            if (callback(err, data) === true) {
                //if that is block of flow then return here not processing any other 
                return;
            }
        }
        //TODO
        for(let cb of this.onDataCb){
            cb(err, data);
        }
        
    }

    dataReceiver(buff) {
        try {
            let data_packets = this.checkDeviceData(buff);
            
            //if(buff.toString().indexOf('payload')>=0)console.log('data--->',data_packets.map(v=>v.toString()));

            if (!data_packets) return; //data is not completed

            if (data_packets.length == 0) return this.dataReceiveHandel(new Error('CRC validity error detected.'));

            for (let data of data_packets) {
                this.dataReceiveHandel(null, data);
            }
        }
        catch (err) {
            this.dataReceiveHandel(err);
        }
    }

    onData(callback) {
        if (typeof callback == 'function')this.onDataCb.push(callback);
    }

    removeOnData(callback=null){
        if(callback===null){
            this.onDataCb=[]; //clear
        }
        else {
            let f=this.onDataCb.indexOf(callback);
            if(f != -1)this.onDataCb.splice(f,1); //delete
        }
    }

    waitToReceiveData(timeout = 1500, block_flow = true) {
        return new Promise((accept, reject) => {
            let cb = (err, data) => {
                clearTimeout(timeout_handeler);
                if (err) return reject(err);
                accept(data);

                return block_flow;
            };

            let timeout_handeler = setTimeout(() => {

                let f = this.dataReceiverHolder.indexOf(cb);
                if (f != -1) {
                    //console.log('cb clearing:');
                    this.dataReceiverHolder.splice(f, 1);
                }

                reject(new Error("Driver is not responding with in timeout"));
            }, timeout);

            this.dataReceiverHolder.push(cb);
        });
    }

    async waitToReceiveJsonData(timeout = 1500, block_flow = true) {
        let data = await this.waitToReceiveData(timeout, block_flow);
        try {
            return PacketDevice.jsonParse(data);
        }
        catch (err) {
            throw new Error("Data parsing error because -> " + data);
        }
    }

    async waitToReceiveParsedData(timeout = 1500, block_flow = true) {
        let data = await this.waitToReceiveData(timeout, block_flow);
        try {
            return PacketDevice.dataParse(data);
        }
        catch (err) {
            //console.log('Parse Error:',err);
            throw new Error("Data parsing error because -> " + data);
        }
    }

    waitUntillFound(valid_data, timeout = 1500) {
        return new Promise((accept,reject)=>{

            let result_cb=(result)=>{
                clearTimeout(timeout_handeler);
                this.removeOnData(cb);
                accept(result);
            }

            let cb = (err, data) => {
                //console.log('extra receiver');
                if (err) return reject(err);
                
                try{
                    let parsed_data = PacketDevice.dataParse(data);
                    
                    if (Array.isArray(valid_data) && typeof parsed_data == 'object') {
                        for (let key of valid_data) {
                            if (key in parsed_data) return result_cb(parsed_data);
                        }
                    }
                    else if (typeof parsed_data == typeof valid_data) {
                        if (typeof valid_data == 'object') {
                            for (let key in valid_data) {
                                if (key in parsed_data && parsed_data[key] == valid_data[key]) return result_cb(parsed_data);
                            }
                        }
                        else if (parsed_data == valid_data) return result_cb(parsed_data);
                    }
                }
                catch(err){
                    //no error handel here
                }
            };

            let timeout_handeler = setTimeout(() => {
                this.removeOnData(cb);
                reject(new Error(`No valid response within ${timeout}ms.`));
            }, timeout);

            this.onData(cb);
        });
    }
}