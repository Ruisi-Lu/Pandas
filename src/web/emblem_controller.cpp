﻿// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "emblem_controller.hpp"

#include <fstream>
#include <iostream>
#include <ostream>

#include "../common/showmsg.hpp"
#include "../common/socket.hpp"

#include "auth.hpp"
#include "http.hpp"
#include "sqllock.hpp"
#include "web.hpp"

// Max size is 50kb for gif
#define MAX_EMBLEM_SIZE 50000
#define START_VERSION 1

std::string getUniqueFileName(const int guild_id, const std::string& world_name, const int version) {
	auto stream = std::ostringstream();
	stream << world_name << "_" << guild_id << "_" << version;
	return stream.str();
}

HANDLER_FUNC(emblem_download) {
	if (!isAuthorized(req, false)) {
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	bool fail = false;
	if (!req.has_file("GDID")) {
		ShowDebug("Missing GuildID field for emblem download.\n");
		fail = true;
	}
	if (!req.has_file("WorldName")) {
		ShowDebug("Missing WorldName field for emblem download.\n");
		fail = true;
	}
	if (fail) {
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	auto world_name_str = req.get_file_value("WorldName").content;
	auto world_name = world_name_str.c_str();
	auto guild_id = std::stoi(req.get_file_value("GDID").content);

#ifdef Pandas_WebServer_Database_EncodingAdaptive
	auto world_name_str_we = U2AWE(world_name_str);
	auto world_name_we = world_name_str_we.c_str();
#endif // Pandas_WebServer_Database_EncodingAdaptive

	SQLLock sl(WEB_SQL_LOCK);
	sl.lock();
	auto handle = sl.getHandle();
	SqlStmt * stmt = SqlStmt_Malloc(handle);
	if (SQL_SUCCESS != SqlStmt_Prepare(stmt,
			"SELECT `version`, `file_type`, `file_data` FROM `%s` WHERE (`guild_id` = ? AND `world_name` = ?)",
			guild_emblems_table)
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 0, SQLDT_INT, &guild_id, sizeof(guild_id))
#ifndef Pandas_WebServer_Database_EncodingAdaptive
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 1, SQLDT_STRING, (void *)world_name, strlen(world_name))
#else
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 1, SQLDT_STRING, (void *)world_name_we, strlen(world_name_we))
#endif // Pandas_WebServer_Database_EncodingAdaptive
		|| SQL_SUCCESS != SqlStmt_Execute(stmt)
	) {
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		sl.unlock();
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	uint32 version = 0;
	char filetype[256];
	char blob[MAX_EMBLEM_SIZE]; // yikes
	uint32 emblem_size;

	if (SqlStmt_NumRows(stmt) <= 0) {
		SqlStmt_Free(stmt);
#ifndef Pandas_WebServer_Console_EncodingAdaptive
		ShowError("[GuildID: %d / World: \"%s\"] Not found in table\n", guild_id, world_name);
#else
		ShowError("[GuildID: %d / World: \"%s\"] Not found in table\n", guild_id, U2ACE(world_name).c_str());
#endif // Pandas_WebServer_Console_EncodingAdaptive
		sl.unlock();
		res.status = 404;
		res.set_content("Error", "text/plain");
		return;
	}


	if (SQL_SUCCESS != SqlStmt_BindColumn(stmt, 0, SQLDT_UINT32, &version, sizeof(version), nullptr, nullptr)
		|| SQL_SUCCESS != SqlStmt_BindColumn(stmt, 1, SQLDT_STRING, &filetype, sizeof(filetype), nullptr, nullptr)
		|| SQL_SUCCESS != SqlStmt_BindColumn(stmt, 2, SQLDT_BLOB, &blob, MAX_EMBLEM_SIZE, &emblem_size, nullptr)
		|| SQL_SUCCESS != SqlStmt_NextRow(stmt)
	) {
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		sl.unlock();
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	SqlStmt_Free(stmt);
	sl.unlock();

	if (emblem_size > MAX_EMBLEM_SIZE) {
		ShowDebug("Emblem is too big, current size is %d and the max length is %d.\n", emblem_size, MAX_EMBLEM_SIZE);
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}
	const char * content_type;
	if (!strcmp(filetype, "BMP"))
		content_type = "image/bmp";
	else if (!strcmp(filetype, "GIF"))
		content_type = "image/gif";
	else {
		ShowError("Invalid image type %s, rejecting!\n", filetype);
		res.status = 404;
		res.set_content("Error", "text/plain");
		return;
	}

	res.body.assign(blob, emblem_size);
	res.set_header("Content-Type", content_type);
}


HANDLER_FUNC(emblem_upload) {
	if (!isAuthorized(req, true)) {
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	bool fail = false;
	if (!req.has_file("GDID")) {
		ShowDebug("Missing GuildID field for emblem download.\n");
		fail = true;
	}
	if (!req.has_file("WorldName")) {
		ShowDebug("Missing WorldName field for emblem download.\n");
		fail = true;
	}
	if (!req.has_file("Img")) {
		ShowDebug("Missing Img field for emblem download.\n");
		fail = true;
	}
	if (!req.has_file("ImgType")) {
		ShowDebug("Missing ImgType for emblem download.\n");
		fail = true;
	}
	if (fail) {
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	auto world_name_str = req.get_file_value("WorldName").content;
	auto world_name = world_name_str.c_str();
	auto guild_id = std::stoi(req.get_file_value("GDID").content);
	auto imgtype_str = req.get_file_value("ImgType").content;
	auto imgtype = imgtype_str.c_str();
	auto img = req.get_file_value("Img").content;
	auto img_cstr = img.c_str();

#ifdef Pandas_WebServer_Database_EncodingAdaptive
	auto world_name_str_we = U2AWE(world_name_str);
	auto world_name_we = world_name_str_we.c_str();
#endif // Pandas_WebServer_Database_EncodingAdaptive
	
	if (imgtype_str != "BMP" && imgtype_str != "GIF") {
		ShowError("Invalid image type %s, rejecting!\n", imgtype);
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	auto length = img.length();
	if (length > MAX_EMBLEM_SIZE) {
		ShowDebug("Emblem is too big, current size is %lu and the max length is %d.\n", length, MAX_EMBLEM_SIZE);
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	if (imgtype_str == "GIF") {
		if (!web_config.allow_gifs) {
			ShowDebug("Client sent GIF image but GIF image support is disabled.\n");
			res.status = 400;
			res.set_content("Error", "text/plain");
			return;
		}
		if (img.substr(0, 3) != "GIF") {
			ShowDebug("Server received ImgType GIF but received file does not start with \"GIF\" magic header.\n");
			res.status = 400;
			res.set_content("Error", "text/plain");
			return;
		}
	}
	else if (imgtype_str == "BMP") {
		if (length < 14) {
			ShowDebug("File size is too short\n");
			res.status = 400;
			res.set_content("Error", "text/plain");
			return;
		}
		if (img.substr(0, 2) != "BM") {
			ShowDebug("Server received ImgType BMP but received file does not start with \"BM\" magic header.\n");
			res.status = 400;
			res.set_content("Error", "text/plain");
			return;
		}
		if (RBUFL(img_cstr, 2) != length) {
			ShowDebug("Bitmap size doesn't match size in file header.\n");
			res.status = 400;
			res.set_content("Error", "text/plain");
			return;
		}

		if (web_config.emblem_transparency_limit < 100) {
			uint32 offset = RBUFL(img_cstr, 0x0A);
			int i, transcount = 1, tmp[3];
			for (i = offset; i < length - 1; i++) {
				int j = i % 3;
				tmp[j] = RBUFL(img_cstr, i);
				if (j == 2 && (tmp[0] == 0xFFFF00FF) && (tmp[1] == 0xFFFF00) && (tmp[2] == 0xFF00FFFF)) //if pixel is transparent
					transcount++;
			}
			if (((transcount * 300) / (length - offset)) > web_config.emblem_transparency_limit) {
				ShowDebug("Bitmap transparency check failed.\n");
				res.status = 400;
				res.set_content("Error", "text/plain");
				return;
			}
		}
	}

	SQLLock sl(WEB_SQL_LOCK);
	sl.lock();
	auto handle = sl.getHandle();
	SqlStmt * stmt = SqlStmt_Malloc(handle);
	if (SQL_SUCCESS != SqlStmt_Prepare(stmt,
			"SELECT `version` FROM `%s` WHERE (`guild_id` = ? AND `world_name` = ?)",
			guild_emblems_table)
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 0, SQLDT_INT, &guild_id, sizeof(guild_id))
#ifndef Pandas_WebServer_Database_EncodingAdaptive
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 1, SQLDT_STRING, (void *)world_name, strlen(world_name))
#else
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 1, SQLDT_STRING, (void *)world_name_we, strlen(world_name_we))
#endif // Pandas_WebServer_Database_EncodingAdaptive
		|| SQL_SUCCESS != SqlStmt_Execute(stmt)
	) {
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		sl.unlock();
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	uint32 version = START_VERSION;

	if (SqlStmt_NumRows(stmt) > 0) {
		if (SQL_SUCCESS != SqlStmt_BindColumn(stmt, 0, SQLDT_UINT32, &version, sizeof(version), NULL, NULL)
			|| SQL_SUCCESS != SqlStmt_NextRow(stmt)
		) {
			SqlStmt_ShowDebug(stmt);
			SqlStmt_Free(stmt);
			sl.unlock();
			res.status = 400;
			res.set_content("Error", "text/plain");
			return;
		}
		version += 1;
	}

	// insert new
	if (SQL_SUCCESS != SqlStmt_Prepare(stmt,
		"REPLACE INTO `%s` (`version`, `file_type`, `guild_id`, `world_name`, `file_data`) VALUES (?, ?, ?, ?, ?)",
		guild_emblems_table)
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 0, SQLDT_UINT32, &version, sizeof(version))
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 1, SQLDT_STRING, (void *)imgtype, strlen(imgtype))
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 2, SQLDT_INT, &guild_id, sizeof(guild_id))
#ifndef Pandas_WebServer_Database_EncodingAdaptive
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 3, SQLDT_STRING, (void *)world_name, strlen(world_name))
#else
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 3, SQLDT_STRING, (void *)world_name_we, strlen(world_name_we))
#endif // Pandas_WebServer_Database_EncodingAdaptive
		|| SQL_SUCCESS != SqlStmt_BindParam(stmt, 4, SQLDT_BLOB, (void *)img.c_str(), length)
		|| SQL_SUCCESS != SqlStmt_Execute(stmt)
	) {
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		sl.unlock();
		res.status = 400;
		res.set_content("Error", "text/plain");
		return;
	}

	SqlStmt_Free(stmt);
	sl.unlock();

	std::ostringstream stream;
	stream << "{\"Type\":1,\"version\":" << version << "}";
	res.set_content(stream.str(), "application/json");
}
