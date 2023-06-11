#ifndef ADDRESS_MAPPING_UNIT_PAGE_LEVEL
#define ADDRESS_MAPPING_UNIT_PAGE_LEVEL

#include <unordered_map>
#include <map>
#include <queue>
#include <set>
#include <list>
#include "Address_Mapping_Unit_Base.h"
#include "Flash_Block_Manager_Base.h"
#include "SSD_Defs.h"
#include "NVM_Transaction_Flash_RD.h"
#include "NVM_Transaction_Flash_WR.h"

namespace SSD_Components
{
#define MAKE_TABLE_INDEX(LPN,STREAM)

	enum class CMTEntryStatus {FREE, WAITING, VALID};

	struct GTDEntryType //Entry type for the Global Translation Directory
	{
		MPPN_type MPPN;
		data_timestamp_type TimeStamp;
	};

	struct CMTSlotType
	{
		PPA_type PPA;
		unsigned long long WrittenStateBitmap;
		bool Dirty;
		CMTEntryStatus Status;
		std::list<std::pair<LPA_type, CMTSlotType*>>::iterator listPtr;//used for fast implementation of LRU
		stream_id_type Stream_id;
	};

	struct GMTEntryType//Entry type for the Global Mapping Table
	{
		PPA_type PPA;
		uint64_t WrittenStateBitmap;
		data_timestamp_type TimeStamp;
	};
	
	//** GMT equals to Primary Mapping Table in CAFTL
	//** CAFTL maintains SMT (Secondary Mapping Table) for VPN-to-PPN mapping
	struct SMTEntryType//** Append for CAFTL
	{
		PPA_type PPA;
	};

	struct ChunkInfo//** Append for CAFTL
	{
		PPA_type PPA;
		size_t ref;//number of this chunk appear
	}; 

	struct RMEntryType//** Append for CAFTL reverse mapping, it replaces metadata in OOB
	{
		FP_type FP;
		LPA_type LPA;
		VPA_type VPA;
		bool use_SMT;//For unique chunk but using two-level mapping (ref decreases to 1)
		bool status;
	};

	class Deduplicator//** Append for CAFTL
	{
	public:
		Deduplicator();
		~Deduplicator();
		void Update_FPtable(const std::pair<FP_type, ChunkInfo> &FP_entry);
		void Print_FPtable();
		bool In_FPtable(const FP_type &FP);//** Check if this FP exists in hash table
		ChunkInfo GetChunkInfo(const FP_type &FP);
		float Get_DedupRate();
		size_t Get_FPtable_size();

		size_t Total_chunk_no;//total chunk(page), including unique and deduped chunks
		size_t Dup_chunk_no;//discarded chunks
		
	private: 
		std::unordered_map<FP_type, ChunkInfo> FPtable;
		float Dedup_rate;
	};

	static std::map<PPA_type, RMEntryType> ReverseMapping;//** Simplify read operation for metadata in OOB e.g., FP by using <PPA, FP> structure to update FP table
	void Update_ReverseMapping(const std::pair<PPA_type, RMEntryType> &cur_rev_pair);
	void Delete_ReverseMapping(const PPA_type &target_ppa);
	void Print_ReverseMapping();
	void Get_metadata_from_ReverseMapping(const PPA_type &moving_ppa, RMEntryType &metadata);
	void Get_LPA_from_ReverseMapping(const PPA_type &target_ppa, LPA_type &LPA);

	static std::map<VPA_type, SMTEntryType> SecondaryMappingTable;//** For CAFTL 2-level mapping while GMT as Primary Mapping Table in CAFTL. It should be inserted as pair <VPA, PPA>

	void Print_SMT();
	bool In_SMT(VPA_type VPA);
	SMTEntryType Get_SMTEntry(VPA_type VPA);
	void Update_SMT(const std::pair<VPA_type, SMTEntryType> &cur_SMT_pair);

	/* Latency (microsecond) */
	const float page_FP_latency = 6.4;
	const float page_write_latency = 200;
	const float page_read_latency = 25;

	class Cached_Mapping_Table
	{
	public:
		Cached_Mapping_Table(unsigned int capacity);
		~Cached_Mapping_Table();
		bool Exists(const stream_id_type streamID, const LPA_type lpa);
		PPA_type Retrieve_ppa(const stream_id_type streamID, const LPA_type lpa);
		void Update_mapping_info(const stream_id_type streamID, const LPA_type lpa, const PPA_type ppa, const page_status_type pageWriteState);
		void Insert_new_mapping_info(const stream_id_type streamID, const LPA_type lpa, const PPA_type ppa, const unsigned long long pageWriteState);
		page_status_type Get_bitmap_vector_of_written_sectors(const stream_id_type streamID, const LPA_type lpa);

		bool Is_slot_reserved_for_lpn_and_waiting(const stream_id_type streamID, const LPA_type lpa);
		bool Check_free_slot_availability();
		void Reserve_slot_for_lpn(const stream_id_type streamID, const LPA_type lpa);
		CMTSlotType Evict_one_slot(LPA_type& lpa);
		
		bool Is_dirty(const stream_id_type streamID, const LPA_type lpa);
		void Make_clean(const stream_id_type streamID, const LPA_type lpa);
	private:
		std::unordered_map<LPA_type, CMTSlotType*> addressMap;
		std::list<std::pair<LPA_type, CMTSlotType*>> lruList;
		unsigned int capacity;
	};

	/* Each stream has its own address mapping domain. It helps isolation of GC interference
	* (e.g., multi-streamed SSD HotStorage 2014, and OPS isolation in FAST 2015)
	* However, CMT is shared among concurrent streams in two ways: 1) each address mapping domain
	* shares the whole CMT space with other domains, and 2) each address mapping domain has
	* its own share of CMT (equal partitioning of CMT space among concurrent streams).*/

	class AddressMappingDomain
	{
	public:
		AddressMappingDomain(unsigned int cmt_capacity, unsigned int cmt_entry_size, unsigned int no_of_translation_entries_per_page,
			Cached_Mapping_Table* CMT,
			Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme,
			flash_channel_ID_type* channel_ids, unsigned int channel_no, flash_chip_ID_type* chip_ids, unsigned int chip_no,
			flash_die_ID_type* die_ids, unsigned int die_no, flash_plane_ID_type* plane_ids, unsigned int plane_no,
			PPA_type total_physical_sectors_no, LHA_type total_logical_sectors_no, unsigned int sectors_no_per_page);
		~AddressMappingDomain();

		/*Stores the mapping of Virtual Translation Page Number (MVPN) to Physical Translation Page Number (MPPN).
		* It is always kept in volatile memory.*/
		GTDEntryType* GlobalTranslationDirectory;

		/*The cached mapping table that is implemented based on the DFLT (Gupta et al., ASPLOS 2009) proposal.
		* It is always stored in volatile memory.*/
		unsigned int CMT_entry_size;
		unsigned int Translation_entries_per_page;
		Cached_Mapping_Table* CMT;
		unsigned int No_of_inserted_entries_in_preconditioning;

		/*The logical to physical address mapping of all data pages that is implemented based on the DFTL (Gupta et al., ASPLOS 2009(
		* proposal. It is always stored in non-volatile flash memory.*/
		GMTEntryType* GlobalMappingTable;

		void Update_mapping_info(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa, const PPA_type ppa, const page_status_type page_status_bitmap);
		page_status_type Get_page_status(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa);
		PPA_type Get_ppa(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa);
		PPA_type Get_ppa_for_preconditioning(const stream_id_type stream_id, const LPA_type lpa);
		bool Mapping_entry_accessible(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa);
	
		std::multimap<LPA_type, NVM_Transaction_Flash*> Waiting_unmapped_read_transactions;
		std::multimap<LPA_type, NVM_Transaction_Flash*> Waiting_unmapped_program_transactions;
		std::multimap<MVPN_type, LPA_type> ArrivingMappingEntries;
		std::set<MVPN_type> DepartingMappingEntries;
		std::set<LPA_type> Locked_LPAs;//Used to manage race conditions, i.e. a user request accesses and LPA while GC is moving that LPA 
		std::set<MVPN_type> Locked_MVPNs;//Used to manage race conditions
		std::multimap<LPA_type, NVM_Transaction_Flash*> Read_transactions_behind_LPA_barrier;
		std::multimap<LPA_type, NVM_Transaction_Flash*> Write_transactions_behind_LPA_barrier;
		std::set<MVPN_type> MVPN_read_transactions_waiting_behind_barrier;
		std::set<MVPN_type> MVPN_write_transaction_waiting_behind_barrier;

		Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme;
		flash_channel_ID_type* Channel_ids;
		unsigned int Channel_no;
		flash_chip_ID_type* Chip_ids;
		unsigned int Chip_no;
		flash_die_ID_type* Die_ids;
		unsigned int Die_no;
		flash_plane_ID_type* Plane_ids;
		unsigned int Plane_no;

		LHA_type max_logical_sector_address;
		LPA_type Total_logical_pages_no;
		PPA_type Total_physical_pages_no;
		MVPN_type Total_translation_pages_no;
		void Print_PMT();

		//** Append for CAFTL
		Deduplicator *deduplicator;
		
		void Print_Mappings_Detail() {
			deduplicator->Print_FPtable();
			Print_PMT();
			Print_SMT();
			Print_ReverseMapping();
		}

		std::ifstream fp_input_file;//** Append for CAFTL fp input
		FP_type cur_fp;//** Record current fp
		size_t Total_fp_no;//** Total number of fingerprints by trace

		size_t Total_page_write_no;//** partial and full write, including GC write 
		size_t GC_page_write_no;

		float Total_write_time;//** latency with FPing
		float Total_read_time;

		std::ofstream DedupOutputFile;
	};

	class Address_Mapping_Unit_Page_Level : public Address_Mapping_Unit_Base
	{
		friend class GC_and_WL_Unit_Page_Level;
	public:
		Address_Mapping_Unit_Page_Level(const sim_object_id_type& id, FTL* ftl, NVM_PHY_ONFI* flash_controller, Flash_Block_Manager_Base* block_manager,
			bool ideal_mapping_table, unsigned int cmt_capacity_in_byte, Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme,
			unsigned int ConcurrentStreamNo,
			unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int DieNoPerChip, unsigned int PlaneNoPerDie,
			std::vector<std::vector<flash_channel_ID_type>> stream_channel_ids, std::vector<std::vector<flash_chip_ID_type>> stream_chip_ids,
			std::vector<std::vector<flash_die_ID_type>> stream_die_ids, std::vector<std::vector<flash_plane_ID_type>> stream_plane_ids,
			unsigned int Block_no_per_plane, unsigned int Page_no_per_block, unsigned int SectorsPerPage, unsigned int PageSizeInBytes,
			double Overprovisioning_ratio, CMT_Sharing_Mode sharing_mode = CMT_Sharing_Mode::SHARED, bool fold_large_addresses = true);
		~Address_Mapping_Unit_Page_Level();
		void Setup_triggers();
		void Start_simulation();
		void Validate_simulation_config();
		void Execute_simulator_event(MQSimEngine::Sim_Event*);

		void Allocate_address_for_preconditioning(const stream_id_type stream_id, std::map<LPA_type, page_status_type>& lpa_list, std::vector<double>& steady_state_distribution);
		int Bring_to_CMT_for_preconditioning(stream_id_type stream_id, LPA_type lpa);
		unsigned int Get_cmt_capacity();
		unsigned int Get_current_cmt_occupancy_for_stream(stream_id_type stream_id);
		void Translate_lpa_to_ppa_and_dispatch(const std::list<NVM_Transaction*>& transactionList);
		void Get_data_mapping_info_for_gc(const stream_id_type stream_id, const LPA_type lpa, PPA_type& ppa, page_status_type& page_state);
		void Get_translation_mapping_info_for_gc(const stream_id_type stream_id, const MVPN_type mvpn, MPPN_type& mppa, sim_time_type& timestamp);
		void Allocate_new_page_for_gc(NVM_Transaction_Flash_WR* transaction, bool is_translation_page);

		void Store_mapping_table_on_flash_at_start();
		LPA_type Get_logical_pages_count(stream_id_type stream_id);
		NVM::FlashMemory::Physical_Page_Address Convert_ppa_to_address(const PPA_type ppa);
		void Convert_ppa_to_address(const PPA_type ppn, NVM::FlashMemory::Physical_Page_Address& address);
		PPA_type Convert_address_to_ppa(const NVM::FlashMemory::Physical_Page_Address& pageAddress);

		void Set_barrier_for_accessing_physical_block(const NVM::FlashMemory::Physical_Page_Address& block_address);
		void Set_barrier_for_accessing_lpa(stream_id_type stream_id, LPA_type lpa);
		void Set_barrier_for_accessing_mvpn(stream_id_type stream_id, MVPN_type mpvn);
		void Remove_barrier_for_accessing_lpa(stream_id_type stream_id, LPA_type lpa);
		void Remove_barrier_for_accessing_mvpn(stream_id_type stream_id, MVPN_type mpvn);
		void Start_servicing_writes_for_overfull_plane(const NVM::FlashMemory::Physical_Page_Address plane_address);

		//** Append for CAFTL
		size_t Total_write;
		size_t Total_read;
		size_t read_before_write;
	private:
		static Address_Mapping_Unit_Page_Level* _my_instance;
		unsigned int cmt_capacity;
		AddressMappingDomain** domains;
		unsigned int CMT_entry_size, GTD_entry_size;//In CMT MQSim stores (lpn, ppn, page status bits) but in GTD it only stores (ppn, page status bits)
		void allocate_plane_for_user_write(NVM_Transaction_Flash_WR* transaction);
		void allocate_page_in_plane_for_user_write(NVM_Transaction_Flash_WR* transaction, bool is_for_gc);
		void allocate_plane_for_translation_write(NVM_Transaction_Flash* transaction);
		void allocate_page_in_plane_for_translation_write(NVM_Transaction_Flash* transaction, MVPN_type mvpn, bool is_for_gc);
		void allocate_plane_for_preconditioning(stream_id_type stream_id, LPA_type lpn, NVM::FlashMemory::Physical_Page_Address& targetAddress);
		bool request_mapping_entry(const stream_id_type streamID, const LPA_type lpn);
		static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
		bool translate_lpa_to_ppa(stream_id_type streamID, NVM_Transaction_Flash* transaction);
		std::set<NVM_Transaction_Flash_WR*>**** Write_transactions_for_overfull_planes;

		void generate_flash_read_request_for_mapping_data(const stream_id_type streamID, const LPA_type lpn);
		void generate_flash_writeback_request_for_mapping_data(const stream_id_type streamID, const LPA_type lpn);

		unsigned int no_of_translation_entries_per_page;
		MVPN_type get_MVPN(const LPA_type lpn, stream_id_type stream_id);
		LPA_type get_start_LPN_in_MVP(const MVPN_type);
		LPA_type get_end_LPN_in_MVP(const MVPN_type);

		bool query_cmt(NVM_Transaction_Flash* transaction);
		PPA_type online_create_entry_for_reads(LPA_type lpa, const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address& read_address, uint64_t read_sectors_bitmap);
		void mange_unsuccessful_translation(NVM_Transaction_Flash* transaction);
		void manage_user_transaction_facing_barrier(NVM_Transaction_Flash* transaction);
		void manage_mapping_transaction_facing_barrier(stream_id_type stream_id, MVPN_type mvpn, bool read);
		bool is_lpa_locked_for_gc(stream_id_type stream_id, LPA_type lpa);
		bool is_mvpn_locked_for_gc(stream_id_type stream_id, MVPN_type mvpn);
	};

}

#endif // !ADDRESS_MAPPING_UNIT_PAGE_LEVEL
