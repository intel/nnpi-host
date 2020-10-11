/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <map>
#include <mutex>
#include "nnpiCopyCommand.h"
#include "nnpiDevNet.h"
#include "nnpiInfReq.h"
#include "nnpiCommandList.h"

class nnpiContextObjDB {
public:
	typedef std::pair<uint16_t, uint16_t> id_pair;

	nnpiContextObjDB()
	{
	}

	void insertCopy(uint16_t id, nnpiCopyCommand::ptr copy)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_copies[id] = copy;
	}

	void removeCopy(uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_copies.erase(id);
	}

	nnpiCopyCommand::ptr getCopy(uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		return m_copies[id];
	}

	void insertDevNet(uint16_t id, nnpiDevNet::ptr devnet)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_networks[id] = devnet;
	}

	void removeDevNet(uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_networks.erase(id);
	}

	nnpiDevNet::ptr getDevNet(uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		return m_networks[id];
	}

	void insertInfReq(uint16_t id, nnpiInfReq::ptr infreq)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_infreqs[id_pair(infreq->network()->id(), id)] = infreq;
	}

	void removeInfReq(uint16_t net_id, uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_infreqs.erase(id_pair(net_id, id));
	}

	nnpiInfReq::ptr getInfReq(uint16_t net_id, uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		return m_infreqs[id_pair(net_id, id)];
	}

	void insertCommandList(uint16_t id, nnpiCommandList::ptr cmdlist)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_cmdlists[id] = cmdlist;
	}

	void removeCommandList(uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_cmdlists.erase(id);
	}

	nnpiCommandList::ptr getCommandList(uint16_t id)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		return m_cmdlists[id];
	}

	void clearAll()
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_cmdlists.clear();
		m_infreqs.clear();
		m_networks.clear();
		m_copies.clear();
	}

	template <class CB_FUNC>
	void for_each_copy(CB_FUNC cb)
	{
		std::lock_guard<std::mutex> lock(m_lock);

		for (std::map<uint16_t, nnpiCopyCommand::ptr>::iterator c = m_copies.begin();
		     c != m_copies.end();
		     c++)
			cb((*c).second);
	}

	template <class CB_FUNC>
	void for_each_cmdlist(CB_FUNC cb)
	{
		std::lock_guard<std::mutex> lock(m_lock);

		for (std::map<uint16_t, nnpiCommandList::ptr>::iterator c = m_cmdlists.begin();
		     c != m_cmdlists.end();
		     c++)
			cb((*c).second);
	}

private:
	std::mutex m_lock;
	std::map<uint16_t, nnpiCopyCommand::ptr> m_copies;
	std::map<uint16_t, nnpiDevNet::ptr> m_networks;
	std::map<id_pair, nnpiInfReq::ptr> m_infreqs;
	std::map<uint16_t, nnpiCommandList::ptr> m_cmdlists;
};
