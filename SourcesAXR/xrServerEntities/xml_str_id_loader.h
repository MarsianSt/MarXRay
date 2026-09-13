#pragma once

#include "../xrCore/TaskManager.h"

#ifdef XRGAME_EXPORTS
#	include "ui/xrUIXmlParser.h"
#else // XRGAME_EXPORTS
#	include "xrUIXmlParser.h"
#	include "object_broker.h"
#endif // XRGAME_EXPORTS


//T_ID    - ���������� ��������� ������������� (�������� id � XML �����)
//T_INDEX - ���������� �������� ������ 
//T_INIT -  ����� ��� ���������� ����������� InitXmlIdToIndex
//          ������� ������������� file_str � tag_name

//��������� ������ ��������� id �������� 
//���� � �������, ��� ���� ������� ���������
struct ITEM_DATA
{
	shared_str		id;
	int				index;
	int				pos_in_file;
	CUIXml*			_xml;
};
typedef xr_vector<ITEM_DATA>	T_VECTOR;

void _destroy_item_data_vector_cont(T_VECTOR* vec);

#define TEMPLATE_SPECIALIZATION template<typename T_INIT>
#define CSXML_IdToIndex CXML_IdToIndex<T_INIT>

TEMPLATE_SPECIALIZATION
class CXML_IdToIndex
{
public:

private:
	static	T_VECTOR*				m_pItemDataVector;

protected:
	//����� xml ������ (����������� �������) �� ������� 
	//����������� �������� ���������
	static LPCSTR					file_str;
	//����� �����
	static LPCSTR					tag_name;
public:
									CXML_IdToIndex							();
	virtual							~CXML_IdToIndex					();

	static	void					InitInternal ();

	static const ITEM_DATA*			GetById		(const shared_str& str_id, bool no_assert = false);
	static const ITEM_DATA*			GetByIndex	(int index, bool no_assert = false);

	static const int			IdToIndex	(const shared_str& str_id, int default_index = T_INDEX(-1), bool no_assert = false)
{
		const ITEM_DATA* item = GetById(str_id, no_assert);
		return item?item->index:default_index;
	}
	static const shared_str		IndexToId	(int index, shared_str default_id = NULL, bool no_assert = false)
	{
		const ITEM_DATA* item = GetByIndex(index, no_assert);
		return item?item->id:default_id;
	}

	static const int		GetMaxIndex	()					{return m_pItemDataVector->size()-1;}
	static const T_VECTOR*	Items		()					{return m_pItemDataVector;}

	//�������� ����������� �������
	static void					DeleteIdToIndexData		();
};


TEMPLATE_SPECIALIZATION
typename T_VECTOR* CSXML_IdToIndex::m_pItemDataVector = NULL;

TEMPLATE_SPECIALIZATION
LPCSTR CSXML_IdToIndex::file_str = NULL;
TEMPLATE_SPECIALIZATION
LPCSTR CSXML_IdToIndex::tag_name = NULL;


TEMPLATE_SPECIALIZATION
CSXML_IdToIndex::CXML_IdToIndex()
{
}


TEMPLATE_SPECIALIZATION
CSXML_IdToIndex::~CXML_IdToIndex()
{
}


TEMPLATE_SPECIALIZATION
const typename ITEM_DATA* CSXML_IdToIndex::GetById (const shared_str& str_id, bool no_assert)
{
	T_INIT::InitXmlIdToIndex();
	T_VECTOR::iterator it = m_pItemDataVector->begin();
	
	for(;m_pItemDataVector->end() != it; it++)
	{
		if( (*it).id == str_id)
			break;
	}

	if(m_pItemDataVector->end() == it)
	{
		int i=0;
		for(T_VECTOR::iterator it = m_pItemDataVector->begin();	m_pItemDataVector->end() != it; it++,i++)
			LogDebug("[%d]=[%s]",i,*(*it).id );

		R_ASSERT3(no_assert, "item not found, id", *str_id);
		return NULL;
	}
		
	return &(*it);
}

TEMPLATE_SPECIALIZATION
const typename ITEM_DATA* CSXML_IdToIndex::GetByIndex(int index, bool no_assert)
{
	if((size_t)index>=m_pItemDataVector->size())
	{
		R_ASSERT3(no_assert, "item by index not found in files", file_str);
		return NULL;
	}
	return &(*m_pItemDataVector)[index];
}

TEMPLATE_SPECIALIZATION
void CSXML_IdToIndex::DeleteIdToIndexData	()
{
	VERIFY						(m_pItemDataVector);
	_destroy_item_data_vector_cont	(m_pItemDataVector);

	xr_delete	(m_pItemDataVector);
}

TEMPLATE_SPECIALIZATION
typename void	CSXML_IdToIndex::InitInternal ()
{
	VERIFY(!m_pItemDataVector);
	T_INIT::InitXmlIdToIndex();

	m_pItemDataVector = xr_new<T_VECTOR>();

	VERIFY(file_str);
	VERIFY(tag_name);

	// Collect full file list (single-threaded string ops)
	xr_vector<xr_string> xml_paths;
	{
		string_path	xml_file;
		int			count = _GetItemCount(file_str);
		if (count)
		{
			xml_paths.reserve	(count);
			for (int it=0; it<count; ++it)
			{
				_GetItem	(file_str, it, xml_file);
				xr_string	full = xml_file;
				full		+= ".xml";
				xml_paths.push_back(full);
			}
		}
	}

	const u32 n = (u32)xml_paths.size();
	if (n == 0)
		return;

	// Phase 1: load & parse each XML in parallel.
	// Each CUIXml/tinyxml instance is self-contained; shared_str docking is
	// mutex-guarded inside the string container and ref-counting is atomic.
	xr_vector<xr_vector<ITEM_DATA>> per_file(n);
	auto parse_file = [&](u32 i)
	{
		CUIXml* uiXml		= xr_new<CUIXml>();
		uiXml->Load			(CONFIG_PATH, "gameplay", xml_paths[i].c_str());

		xr_vector<ITEM_DATA>& out = per_file[i];
		int items_num		= uiXml->GetNodesNum(uiXml->GetRoot(), tag_name);
		out.reserve			(items_num < 0 ? 0 : (size_t)items_num);

		for (int id=0; id<items_num; ++id)
		{
			LPCSTR item_name	= uiXml->ReadAttrib(uiXml->GetRoot(), tag_name, id, "id", NULL);

			string256			buf;
			xr_sprintf			(buf, "id for item don't set, number %d in %s", id, xml_paths[i].c_str());
			R_ASSERT2			(item_name, buf);

			ITEM_DATA			data;
			data.id				= item_name;
			data.index			= -1;			// assigned below on merge
			data.pos_in_file	= id;
			data._xml			= uiXml;
			out.push_back		(data);
		}

		if (0 == items_num)
			delete_data		(uiXml);
	};

	if (CTaskManager::IsInsideTask())
		for (u32 i=0; i<n; ++i)
			parse_file		(i);
	else
	{
		CTaskManager::AddTaskRange(
			[&](u32 start, u32 end, u32)
			{
				for (u32 i=start; i<end; ++i)
					parse_file (i);
			}, n, 1);
		CTaskManager::WaitAll ();
	}

	// Phase 2: merge serially (the duplicate check / global index are order-dependent)
	int		index = 0;
	for (u32 f=0; f<n; ++f)
	{
		for (xr_vector<ITEM_DATA>::iterator it = per_file[f].begin(); it != per_file[f].end(); ++it)
		{
			T_VECTOR::iterator t_it = m_pItemDataVector->begin();
			for(;m_pItemDataVector->end() != t_it; t_it++)
			{
				if(shared_str((*t_it).id) == shared_str((*it).id))
					break;
			}
			R_ASSERT3(m_pItemDataVector->end() == t_it, "duplicate item id", (*it).id.c_str());

			(*it).index		= index++;
			m_pItemDataVector->push_back(*it);
		}
	}
}

#undef TEMPLATE_SPECIALIZATION

