//Packet Details header: <[]-[]*[]-[]>
const PACKET_SIGNETURE_DATA_LEN = 4;
const PACKET_SIGNETURE_LEN = 9;
const packet_maker=Buffer.from('<-*->');
const packet_info = Buffer.from([packet_maker[0], 0x0F,packet_maker[1], 0x0F, packet_maker[2], 0x0F, packet_maker[3], 0x0F, packet_maker[4]]); //packet length signeture

module.exports = class DataEndPusherExtractor {
    delimiter;
    packet_length = 0;
    packet_timeout_at = null;
    store_buff;

    constructor(delimiter = '\r\n') {
        this.delimiter = delimiter;
        this.store_buff = Buffer.from('');
    }

    clear() {
        this.store_buff = Buffer.from('');
    }

    getPacketLength(transfer_buff) {
        if (transfer_buff[0] != packet_info[0] || transfer_buff[PACKET_SIGNETURE_LEN - 1] != packet_info[PACKET_SIGNETURE_LEN - 1]) return 0;

        let packet_size = 0;
        for (let i = 1; i < (PACKET_SIGNETURE_LEN - 1); i++) {
            if (packet_info[i] == 0x0F) {
                //data
                if (transfer_buff[i] > 0x0F) {
                    //if segment is invalid
                    return 0;
                }
                packet_size = (packet_size << 4) | transfer_buff[i];
            } else if (packet_info[i] != transfer_buff[i]) {
                //format is not matching
                return 0;
            }
        }

        return packet_size;
    }

    updatePacketLength(packet_size) {
        let header=Buffer.alloc(PACKET_SIGNETURE_LEN);
        packet_info.copy(header);

        //console.log(packet_size);
        // //{ (packet_size & 0xF000) >> 12, (packet_size & 0x0F00) >> 8, (packet_size & 0x00F0) >> 4, packet_size & 0x000F }
        for (let i = 0; i < PACKET_SIGNETURE_DATA_LEN; i++) {
            header[(i * 2) + 1] = (packet_size  >> (12 - (i * 4))) & 0x0F;
            //console.log((i * 2) + 1,header[(i * 2) + 1],(12 - (i * 4)));
        }

        return header;
    }

    searchPacketMatch(transfer_buffer){
        let search_offset=0;
        while(transfer_buffer.length>search_offset){
            let sfind = transfer_buffer.indexOf(packet_info[0], search_offset);
            if(sfind==-1 || (sfind+PACKET_SIGNETURE_LEN)>transfer_buffer.length)return null;
            search_offset=sfind+PACKET_SIGNETURE_LEN;
            let packet_size=this.getPacketLength(transfer_buffer.subarray(sfind,search_offset));
            if(packet_size!==0){
                return [search_offset,packet_size];
            }
        }
        return null;
    }

    readData() {
        let packets = [];

        if (this.packet_timeout_at !== null && this.packet_length !== 0 && Date.now() > this.packet_timeout_at) {
            this.packet_length = 0;
            this.packet_timeout_at = null;
        }

        let offset = 0;
        // let checker=0;
        while (this.store_buff.length > offset) {

            if (this.packet_length !== 0) {
                if ((this.store_buff.length - offset) >= this.packet_length) {
                    packets.push(Buffer.from(this.store_buff.subarray(offset, offset + this.packet_length))); //taking a new copy
                    offset = offset + this.packet_length;

                    this.packet_length = 0; //reset
                    this.packet_timeout_at = null; //reset timeout
                }
                else break; ///there has no enough data, and need to wait for next data update
            }
            else {
                if ((this.store_buff.length - offset) >= PACKET_SIGNETURE_LEN) {
                    let search_result=this.searchPacketMatch(this.store_buff.subarray(offset,this.store_buff.length));
                    if(search_result!==null){
                        let [packet_end,packet_size]=search_result;
                        offset = offset + packet_end; //increasing offset

                        this.packet_length = packet_size;
                        this.packet_timeout_at = Date.now() + (packet_size * 2) + 100; //minimum baud rate could 4800bps that mean 600bytes for second, considering 2ms for each of byte, and some extra delay (100ms)

                        continue;
                    }

                }

                let find = this.store_buff.indexOf(this.delimiter, offset);
                if (find == -1) break; //there has no enough data

                packets.push(Buffer.from(this.store_buff.subarray(offset, find))); //taking a new copy
                offset = find + this.delimiter.length;
            }
        }

        if (offset !== 0) {
            //remove unnessary buffer
            this.store_buff = Buffer.from(this.store_buff.subarray(offset, this.store_buff.length)); //creating a new copy and destoring old one
        }

        return packets;
    }

    pushOut(data) {
        //console.log('push out');

        this.store_buff = Buffer.concat([this.store_buff, data]);
        let packets = this.readData();

        //console.log('push end');

        if (packets.length == 0) return null;
        return packets;
    }
}