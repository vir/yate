/**
 * libchatbot.js
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#require "libeliza.js"

// Helper function that deletes expire chat instances
// TODO: fix Engine.setInterval() so it doesn't need global functions
function __chatExpireFunc()
{
    var exp = Date.now();
    var empty = true;
    for (var inst in chatWithBot.list) {
	var bot = chatWithBot.list[inst];
	if (bot.expireTime >= exp)
	    empty = false;
	else {
	    Engine.debug(Engine.DebugInfo,"Deleting expired bot " + inst);
	    delete chatWithBot.list[inst];
	}
    }
    if (empty && (chatWithBot.expireInt !== undefined)) {
	Engine.debug(Engine.DebugAll,"No active chats, stopping expirer");
	Engine.clearInterval(chatWithBot.expireInt);
	delete chatWithBot.expireInt;
    }
}

// Chat entry point, expects user text and instance
// Instance can be some unique connection address, phone number, etc.
function chatWithBot(text,instance)
{
    if (!instance)
	return null;
    if (text === null) {
	if (chatWithBot.list[instance] !== undefined) {
	    Engine.debug(Engine.DebugInfo,"Explicitly deleting bot " + instance);
	    delete chatWithBot.list[instance];
	}
	return null;
    }
    var bot = null;
    if (chatWithBot.list)
	bot = chatWithBot.list[instance];
    else
	chatWithBot.list = new Array;
    if (!bot) {
	Engine.debug(Engine.DebugInfo,"Creating chat bot for " + instance);
	bot = new Eliza;
	chatWithBot.list[instance] = bot;
    }
    // Since we usually have no clue a chat has ended we set it to expire
    bot.expireTime = Date.now() + 7200000; // Arbitrarily - 2 hours
    if (chatWithBot.expireInt === undefined) {
	Engine.debug(Engine.DebugAll,"Starting bot expirer");
	chatWithBot.expireInt = Engine.setInterval(__chatExpireFunc,60000);
    }
    return bot.listen(text);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
