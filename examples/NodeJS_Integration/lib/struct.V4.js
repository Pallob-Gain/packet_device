/**
 * @file struct.js
 * @author Pallob K. Gain (pallobkgain@gmail.com)
 * @M10Xcore V2.0 board library for c-struct
 * @version 0.1
 * @date 2023-06-2
 * 
 * @copyright Copyright (c) 2023
 * 
 * Known types: https://github.com/TooTallNate/ref/wiki/Known-%22types%22
*/


const UINT8_T = 1;
const UINT16_T = 2;
const UINT32_T = 3;
const UINT64_T = 4;
const CHAR = 5;
const UCHAR = 6;
const BYTE = 7;
const INT = 8;
const UINT = 9;
const FLOAT = 10;
const DOUBLE = 11;
const LONG = 12;
const ULONG = 13;
const INT8_T = 14;
const INT16_T = 15;
const INT32_T = 16;
const INT64_T = 17;
const BOOL = 18;

class Struct {

    static type = {
        uint8_t: { state: UINT8_T, signed: false, size: 1 },
        uint16_t: { state: UINT16_T, signed: false, size: 2 },
        uint32_t: { state: UINT32_T, signed: false, size: 4 },
        uint64_t: { state: UINT64_T, signed: false, size: 8 },
        char: { state: CHAR, signed: true, size: 1 },
        uchar: { state: UCHAR, signed: false, size: 1 },
        byte: { state: BYTE, signed: false, size: 1 },
        bool: { state: BOOL, signed: false, size: 1 },
        int: { state: INT, signed: true, size: 4 },
        uint: { state: UINT, signed: false, size: 4 },
        float: { state: FLOAT, signed: true, size: 4 },
        double: { state: DOUBLE, signed: true, size: 8 },
        long: { state: LONG, signed: true, size: 8 },
        ulong: { state: ULONG, signed: false, size: 8 },
        int8_t: { state: INT8_T, signed: true, size: 1 },
        int16_t: { state: INT16_T, signed: true, size: 2 },
        int32_t: { state: INT32_T, signed: true, size: 4 },
        int64_t: { state: INT64_T, signed: true, size: 8 },
    };

    static sizeOf(struct_type) {
        if (!struct_type || !struct_type.size) return 0;
        return struct_type.size;
    }

    static arrayType(type, length, converter) {
        return { array_type: true, type, length, size: type.size * length, converter };
    }

    static stringType(max_length) {
        return this.arrayType(this.type.char, max_length, (buff) => {
            let firstZero = buff.indexOf('\0');
            firstZero = firstZero == -1 ? buff.length : firstZero;
            let cBuff = Buffer.alloc(firstZero);
            buff.copy(cBuff, 0, 0, firstZero);
            return cBuff.toString();
        });
    }

    static makeType(struct_type) {
        let size = 0;
        let details = {};
        for (let [label, info] of Object.entries(struct_type)) {
            let __info = Object.assign({ offset: size }, info);
            size += info.size;

            details[label] = __info;
        }

        return { details, size };
    }

    holder; //buffer holder
    struct;

    constructor(struct,shared_buffer = null) {
        if(shared_buffer && !(shared_buffer instanceof SharedArrayBuffer)) throw new Error('shared_buffer must be an instance of SharedArrayBuffer');
        this.struct = struct;

        //console.log({struct});

        this.holder = shared_buffer!==null ? Buffer.from(shared_buffer) : Buffer.alloc(struct.size, 0);
    }

    ref() {
        return this.holder;
    }

    collect(source, source_start = 0) {
        //check if source is buffer
        if (!Buffer.isBuffer(source)) throw new Error('Source is not a Buffer');
        //check size
        if (source.length - source_start < this.holder.length) throw new Error('Source buffer size is smaller than struct size');

        source.copy(this.holder, 0, source_start, source_start + this.holder.length);
    }

    size() {
        return this.holder.length;
    }

    setJson(value) {
        if (!(typeof value == 'object')) throw new Error('Value is not an object');

        for (let name in this.struct.details) {
            //only if the name exist in the value
            if (name in value) {
                if (typeof value[name] == 'object') {
                    if (!('details' in this.struct.details[name])) throw new Error('Type is not a Struct');
                    let child = new Struct(this.struct.details[name]);
                    child.setJson(value[name]);
                    this.set(name, child);
                }
                else if (Array.isArray(value[name]) && ('array_type' in this.struct.details[name] && this.struct.details[name].array_type && 'details' in this.struct.details[name].type)) {
                    this.set(name, value[name].map(item => {
                        let child = new Struct(this.struct.details[name].type);
                        child.setJson(item);
                        return child;
                    }));
                }
                else {
                    this.set(name, value[name]);
                }
            }
        }

        return false;
    }

    valueExtractor(value) {
        if (value instanceof Struct) {
            return value.getJson();
        }
        else if (Array.isArray(value)) {
            return value.map(item => this.valueExtractor(item));
        }

        return value;
    }

    getJson() {
        let result = {};
        for (let name in this.struct.details) {
            let value = this.get(name);
            result[name] = this.valueExtractor(value);
        }
        return result;
    }

    set(label, value) {
        if (!(label in this.struct.details)) throw new Error('Unknown struct property: ' + label);
        let info = this.struct.details[label];
        let data_holder = this.holder;

        if ('details' in info && info.details) {
            if (!(value instanceof Struct)) throw new Error('Value is not a Struct instance');
            let buff = value.ref();
            buff.copy(data_holder, info.offset, 0, Math.min(info.size, buff.length));
        }
        else if ('array_type' in info && info.array_type) {
            //if array of struct 
            if ('details' in info.type && info.type.details) {
                let array_length = info.length;
                for (let idx = 0; idx < array_length; idx++) {
                    let struct_instance = value[idx];
                    if (!(struct_instance instanceof Struct)) throw new Error('Value is not a Struct instance');
                    let offset = info.offset + idx * info.type.size;
                    let buff = struct_instance.ref();
                    buff.copy(data_holder, offset, 0, Math.min(info.type.size, buff.length));
                }
            }
            else {
                let type_state = info.type.state;

                switch (type_state) {
                    case CHAR:
                    case UCHAR:
                        value = value;
                        break;
                    case BOOL:
                        value = Uint8Array.from(value.map(v => v ? 1 : 0)).buffer;
                        break;
                    case BYTE:
                    case UINT8_T:
                        value = Uint8Array.from(value).buffer;
                        break;
                    case UINT16_T:
                        value = Uint16Array.from(value).buffer;
                        break;
                    case UINT32_T:
                    case UINT:
                        value = Uint32Array.from(value).buffer;
                        break;
                    case UINT64_T:
                    case ULONG:
                        value = BigUint64Array.from(value).buffer;
                        break;
                    case INT32_T:
                    case INT:
                        value = Int32Array.from(value).buffer;
                        break;
                    case FLOAT:
                        value = Float32Array.from(value).buffer;
                        break;
                    case DOUBLE:
                        value = Float64Array.from(value).buffer;
                        break;
                    case INT64_T:
                    case LONG:
                        value = BigInt64Array.from(value).buffer;
                        break;
                    case INT8_T:
                        value = Int8Array.from(value).buffer;
                        break;
                    case UINT16_T:
                        value = Int16Array.from(value).buffer;
                        break;
                    default:
                        throw new Error('Unknown array type: ' + type_state);
                }

                let buff = Buffer.from(value);
                //console.log('buff',buff,'Copy:',Math.min(info.size,buff.length));
                buff.copy(data_holder, info.offset, 0, Math.min(info.size, buff.length));
            }
        }
        else {
            let type_state = info.state;

            switch (type_state) {
                case CHAR:
                case UCHAR:
                    data_holder.write(value, info.offset, info.size);
                    break;
                case BOOL:
                    data_holder.writeUInt8(value ? 1 : 0, info.offset);
                    break;
                case BYTE:
                case UINT8_T:
                    data_holder.writeUInt8(value, info.offset);
                    break;
                case UINT16_T:
                    data_holder.writeUInt16LE(value, info.offset);
                    break;
                case UINT32_T:
                case UINT:
                    data_holder.writeUInt32LE(value, info.offset);
                    break;
                case UINT64_T:
                case ULONG:
                    data_holder.writeBigUInt64LE(value, info.offset);
                    break;
                case INT32_T:
                case INT:
                    data_holder.writeInt32LE(value, info.offset);
                    break;
                case FLOAT:
                    data_holder.writeFloatLE(value, info.offset);
                    break;
                case DOUBLE:
                    data_holder.writeDoubleLE(value, info.offset);
                    break;
                case INT64_T:
                case LONG:
                    data_holder.writeBigInt64LE(value, info.offset);
                    break;
                case INT8_T:
                    data_holder.writeInt8(value, info.offset);
                    break;
                case INT16_T:
                    data_holder.writeInt16LE(value, info.offset);
                    break;
                default:
                    throw new Error('Unknown data type: ' + type_state);
            }

        }

        return true;
    }

    get(label) {
        if (!(label in this.struct.details)) throw new Error('Unknown struct property: ' + label);
        let info = this.struct.details[label];
        let data_holder = this.holder;

        let value = null;

        //if another struct
        if ('details' in info && info.details) {
            let section_data = Buffer.from(data_holder.buffer, info.offset, info.size);
            value = new Struct(info);
            value.collect(section_data);
        }
        else if ('array_type' in info && info.array_type) {

            //if array of struct 
            if ('details' in info.type && info.type.details) {
                let section_data = Buffer.from(data_holder.buffer, info.offset, info.size);
                let array_length = info.length;
                value = (new Array(array_length).fill(null)).map((_, idx) => {
                    let struct_instance = new Struct(info.type);
                    let offset = idx * info.type.size;
                    struct_instance.collect(section_data, offset);
                    return struct_instance;
                });
            }
            else {
                //we need to copy the buffer section to the a new buffer otherwise array will fail as the offset need to be multiplied by element size
                const section_data = Buffer.alloc(info.size);
                data_holder.copy(section_data, 0, info.offset, info.offset + info.size);
                //let section_data = Buffer.from(data_holder.buffer, info.offset, info.size);
                let type_state = info.type.state;

                switch (type_state) {
                    case CHAR:
                    case UCHAR:
                        value = Buffer.from(section_data);
                        break;
                    case BOOL:
                        value = Array.from(new Uint8Array(section_data.buffer, section_data.byteOffset, section_data.length / Uint8Array.BYTES_PER_ELEMENT)).map(v => v ? true : false);
                        break;
                    case BYTE:
                    case UINT8_T:
                        value = new Uint8Array(section_data.buffer, section_data.byteOffset, section_data.length / Uint8Array.BYTES_PER_ELEMENT);
                        break;
                    case UINT16_T:
                        value = new Uint16Array(section_data.buffer, section_data.byteOffset, section_data.length / Uint16Array.BYTES_PER_ELEMENT);
                        break;
                    case UINT32_T:
                    case UINT:
                        value = new Uint32Array(section_data.buffer, section_data.byteOffset, section_data.length / Uint32Array.BYTES_PER_ELEMENT);
                        break;
                    case UINT64_T:
                    case ULONG:
                        value = new BigUint64Array(section_data.buffer, section_data.byteOffset, section_data.length / BigUint64Array.BYTES_PER_ELEMENT);
                        break;
                    case INT32_T:
                    case INT:
                        value = new Int32Array(section_data.buffer, section_data.byteOffset, section_data.length / Int32Array.BYTES_PER_ELEMENT);
                        break;
                    case FLOAT:
                        value = new Float32Array(section_data.buffer, section_data.byteOffset, section_data.length / Float32Array.BYTES_PER_ELEMENT);
                        break;
                    case DOUBLE:
                        value = new Float64Array(section_data.buffer, section_data.byteOffset, section_data.length / Float64Array.BYTES_PER_ELEMENT);
                        break;
                    case INT64_T:
                    case LONG:
                        value = new BigInt64Array(section_data.buffer, section_data.byteOffset, section_data.length / BigInt64Array.BYTES_PER_ELEMENT);
                        break;
                    case INT8_T:
                        value = new Int8Array(section_data.buffer, section_data.byteOffset, section_data.length / Int8Array.BYTES_PER_ELEMENT);
                        break;
                    case INT16_T:
                        value = new Int16Array(section_data.buffer, section_data.byteOffset, section_data.length / Int16Array.BYTES_PER_ELEMENT);
                        break;
                    default:
                        throw new Error('Unknown array type: ' + type_state);
                }

                if ('converter' in info && info.converter) {
                    value = info.converter(value);
                }
            }
            //console.log('array check:',value);
        }
        else {
            let section_data = Buffer.from(data_holder.buffer, info.offset, info.size);
            let type_state = info.state;

            switch (type_state) {
                case CHAR:
                case UCHAR:
                    value = section_data.buffer[0];
                    break;
                case BOOL:
                    value = section_data.readUInt8(0) ? true : false;
                    break;
                case BYTE:
                case UINT8_T:
                    value = section_data.readUInt8(0);
                    break;
                case UINT16_T:
                    value = section_data.readUInt16LE(0);
                    break;
                case UINT32_T:
                case UINT:
                    value = section_data.readUInt32LE(0);
                    break;
                case UINT64_T:
                case ULONG:
                    value = section_data.readBigUInt64LE(0);
                    break;
                case INT32_T:
                case INT:
                    value = section_data.readInt32LE(0);
                    break;
                case FLOAT:
                    value = section_data.readFloatLE(0);
                    break;
                case DOUBLE:
                    value = section_data.readDoubleLE(0);
                    break;
                case INT64_T:
                case LONG:
                    value = section_data.readBigInt64LE(0);
                    break;
                case INT8_T:
                    value = section_data.readInt8(0);
                    break;
                case INT16_T:
                    value = section_data.readInt16LE(0);
                    break;
                default:
                    throw new Error('Unknown data type: ' + type_state);
            }

        }

        return value;
    }

    data() {
        let result = {};

        for (let [label, info] of Object.entries(this.struct.details)) {

            result[label] = this.get(label);
        }
        return result;
    }
}

module.exports = Struct;