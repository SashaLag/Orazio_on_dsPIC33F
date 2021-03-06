#include <string.h>
#include <assert.h>
#include "uart.h"
#include "deferred_packet_handler.h"
#include "orazio_packets.h"
#include "orazio_globals.h"
#include "orazio_comm.h"
#include "orazio_drive.h"
#include "orazio_param.h"

// these are the possible incoming packets, each with a buffer of 2
#define PBUFNO 2
ParamControlPacket param_control_packet_buffer[PBUFNO];
SystemParamPacket system_param_packet_buffer[PBUFNO];
JointControlPacket  joint_control_packet_buffer[PBUFNO];
JointParamPacket  joint_param_packet_buffer[PBUFNO];
DifferentialDriveControlPacket differential_drive_control_packet_buffer[PBUFNO];
DifferentialDriveParamPacket differential_drive_param_packet_buffer[PBUFNO];

// stuff used in this module
static struct myUART* uart;

static DeferredPacketHandler packet_handler;
static uint16_t global_seq;
static ResponsePacket response={
  {.type=RESPONSE_PACKET_ID,
   .size=sizeof(ResponsePacket),
   .seq=0
  }
};

static EndEpochPacket end_epoch = {
  .type=END_EPOCH_PACKET_ID,
  .size=sizeof(EndEpochPacket),
  .seq=0
};


// this assembles a response packet
void Orazio_sendResponse(PacketHeader* p, PacketStatus result){
  response.p_seq=p->seq;
  response.p_type=p->type;
  response.p_result=result;
  Orazio_sendPacket((PacketHeader*)&response);
}

// this handler copies the packet in the attribute variable
// stored in args
static PacketStatus Orazio_copyToVar(PacketHeader* p, void* args){
  assert(args);
  memcpy(args, p, p->size);
  system_status.rx_seq=p->seq;
  Orazio_sendResponse(p,Success);
  return Success;
}

// this handler copies the packet in the attribute variable
// stored in args
static PacketStatus Orazio_copyToVarAndDisableDrive(PacketHeader* p, void* args){
  Orazio_driveDisable();
  assert(args);
  memcpy(args, p, p->size);
  system_status.rx_seq=p->seq;
  Orazio_sendResponse(p,Success);
  return Success;
}

// this handler copies the packet in the attribute variable
// stored in args, and sends back a copy of the variable
static PacketStatus Orazio_copyToVarAndSend(PacketHeader* p, void* args){
  assert(args);
  memcpy(args, p, p->size);
  system_status.rx_seq=p->seq;
  switch(p->type){
  case SYSTEM_PARAM_PACKET_ID:
    Orazio_paramSet(ParamSystem);
    break;
  case JOINT_PARAM_PACKET_ID:
    Orazio_paramSet(ParamJoints);
    break;
  case DIFFERENTIAL_DRIVE_PARAM_PACKET_ID:
    Orazio_paramSet(ParamDrive);
    break;
  default:;
  }
  Orazio_sendPacket((PacketHeader*)args);
  Orazio_sendResponse(p,Success);
  return Success;
}

PacketStatus Orazio_handleParamControlPacket(PacketHeader* p, void* args){
  PacketHeader* target;
  ParamControlPacket* ctl=(ParamControlPacket*)p;
  system_status.rx_seq=ctl->header.seq;
  PacketStatus r;
  switch(ctl->param_type){
  case ParamSystem:
    target=(PacketHeader*)&system_params;
    break;
  case ParamJoints:
    target=(PacketHeader*)&joint_params;
    break;
  case ParamDrive:
    target=(PacketHeader*)&drive_params;
    break;
  default:
    target=0;
    r=GenericError;
  }
  switch(ctl->action){
  case ParamRequest:
    r=Success;
    break;
  case ParamLoad:
    r=Orazio_paramLoad(ctl->param_type);
    break;
  case ParamSave:
    r=Orazio_paramSave(ctl->param_type);
    break;
  default:
    r=GenericError;
  }
  if (target){
    Orazio_sendPacket(target);
  }
  Orazio_sendResponse(p,r);
  return Success;
}


void Orazio_initializePackets(void) {
 DeferredPacketHandler_installPacket(&packet_handler,
				      PARAM_CONTROL_PACKET_ID,
				      sizeof(ParamControlPacket),
				      param_control_packet_buffer,
				      PBUFNO,
				      Orazio_handleParamControlPacket,
				      0);

  DeferredPacketHandler_installPacket(&packet_handler,
				      SYSTEM_PARAM_PACKET_ID,
				      sizeof(SystemParamPacket),
				      system_param_packet_buffer,
				      PBUFNO,
				      Orazio_copyToVarAndSend,
				      &system_params);

  DeferredPacketHandler_installPacket(&packet_handler,
				      JOINT_CONTROL_PACKET_ID,
				      sizeof(JointControlPacket),
				      joint_control_packet_buffer,
				      PBUFNO,
				      Orazio_copyToVarAndDisableDrive,
				      &joint_control);
  
  DeferredPacketHandler_installPacket(&packet_handler,
				      JOINT_PARAM_PACKET_ID,
				      sizeof(JointParamPacket),
				      joint_param_packet_buffer,
				      PBUFNO,
				      Orazio_copyToVarAndSend,
				      &joint_params);

  DeferredPacketHandler_installPacket(&packet_handler,
				      DIFFERENTIAL_DRIVE_CONTROL_PACKET_ID,
				      sizeof(DifferentialDriveControlPacket),
				      differential_drive_control_packet_buffer,
				      PBUFNO,
				      Orazio_copyToVar,
				      &drive_control);

  DeferredPacketHandler_installPacket(&packet_handler,
				      DIFFERENTIAL_DRIVE_PARAM_PACKET_ID,
				      sizeof(DifferentialDriveParamPacket),
				      differential_drive_param_packet_buffer,
				      PBUFNO,
				      Orazio_copyToVarAndSend,
				      &drive_params);
}


void Orazio_flushInputBuffers(void) {
   system_status.rx_buffer_size=UART_rxBufferFull(uart);
   while (UART_rxBufferFull(uart)){
    uint8_t c=UART_getChar(uart);
    PacketStatus status=PacketHandler_rxByte(&packet_handler.base_handler, c);
    if(status<0){
      ++system_status.rx_packet_errors;
    }
    if(status==SyncChecksum)
      ++system_status.rx_packets;
  }
}

int Orazio_flushOutputBuffers(void){
  while (packet_handler.base_handler.tx_size)
    UART_putChar(uart, PacketHandler_txByte(&packet_handler.base_handler));
  system_status.tx_buffer_size=UART_txBufferFull(uart);
  return packet_handler.base_handler.tx_size;
}

PacketStatus Orazio_sendPacket(PacketHeader* p){
  p->seq=global_seq;
  PacketStatus status=PacketHandler_sendPacket(&packet_handler.base_handler, p);    
  if (status==Success)
    ++system_status.tx_packets;
  else
    ++system_status.tx_packet_errors;
  return status;
}

void Orazio_commInit(void){
  uart = UART_init("uart_0",system_params.comm_speed);
  global_seq=0;
  DeferredPacketHandler_initialize(&packet_handler);
  Orazio_initializePackets();
}


void Orazio_commHandle(void){
  Orazio_flushInputBuffers();
  ++global_seq;
  system_status.rx_packet_queue=packet_handler.pending_packets_size;
  DeferredPacketHandler_processPendingPackets(&packet_handler);
  if (system_params.periodic_packet_mask & PSystemStatusFlag) 
    Orazio_sendPacket((PacketHeader*)&system_status);
  if (system_params.periodic_packet_mask & PJointStatusFlag)
    Orazio_sendPacket((PacketHeader*)&joint_status);
  if (system_params.periodic_packet_mask & PDriveStatusFlag)
    Orazio_sendPacket((PacketHeader*)&drive_status);
  Orazio_sendPacket((PacketHeader*)&end_epoch);
  Orazio_flushOutputBuffers();
}
