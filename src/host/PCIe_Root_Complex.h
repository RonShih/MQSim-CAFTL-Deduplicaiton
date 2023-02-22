#ifndef PCIE_ROOT_COMPLEX_H
#define PCIE_ROOT_COMPLEX_H

#include "../ssd/Host_Interface_Defs.h"
#include "Host_Defs.h"
#include "PCIe_Message.h"
#include "PCIe_Link.h"
#include "Host_IO_Request.h"
#include "IO_Flow_Base.h"
#include "SATA_HBA.h"


namespace Host_Components
{
	class PCIe_Link;
	class IO_Flow_Base;
	class SATA_HBA;
	class PCIe_Root_Complex
	{
	public:
		PCIe_Root_Complex(PCIe_Link* pcie_link, HostInterface_Types SSD_device_type, SATA_HBA* sata_hba, std::vector<Host_Components::IO_Flow_Base*>* IO_flows);
		~PCIe_Root_Complex() {
			PRINT_MESSAGE("================ NVMe request to host related output ================");
			PRINT_MESSAGE("read_pcie_msg_num: " << read_pcie_msg_num);
			PRINT_MESSAGE("write_pcie_msg_num: " << write_pcie_msg_num);
			PRINT_MESSAGE("In write req: ")
			PRINT_MESSAGE("	NVMe_consume_io_request OOR: " << nvme_io_req_OOR_num);
			PRINT_MESSAGE("	NVMe_consume_io_request: " << nvme_io_req_num);
		}
		void Consume_pcie_message(PCIe_Message* messages)//Modern processors support DDIO, where all writes to memory are going through LLC
		{
			switch (messages->Type)
			{
			case PCIe_Message_Type::READ_REQ:
				read_pcie_msg_num++;
				Read_from_memory(messages->Address, (unsigned int)(intptr_t)messages->Payload);
				break;
			case PCIe_Message_Type::WRITE_REQ:
				write_pcie_msg_num++;
				Write_to_memory(messages->Address, messages->Payload);
				break;
			default:
				break;
			}
			delete messages;
		}
		
		void Write_to_device(uint64_t address, uint16_t write_value);
		void Set_io_flows(std::vector<Host_Components::IO_Flow_Base*>* IO_flows);

		//Append for testing: 
		size_t read_pcie_msg_num;
		size_t write_pcie_msg_num;
		size_t nvme_io_req_num; 
		size_t nvme_io_req_OOR_num;

	private:
		PCIe_Link* pcie_link;
		HostInterface_Types SSD_device_type;
		SATA_HBA * sata_hba;
		std::vector<Host_Components::IO_Flow_Base*>* IO_flows;
		
		void Write_to_memory(const uint64_t address, const void* payload);
		void Read_from_memory(const uint64_t address, const unsigned int size);
	};
}

#endif //!PCIE_ROOT_COMPLEX_H
