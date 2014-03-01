// Chat Bot by George Dunlop, www.peccavi.com
// Note - Eliza is a Classic Model of chat Bots.. but this implementation is mine :)
// May be used/modified if credit line is retained (c) 1997 All rights reserved

// Rewritten, fixed, converted to objects and adapted to Yate by Paul Chitescu


// Constructor
function Eliza(name)
{
    if (name == "")
	name = "Eliza";
    this.name = name;
    this.wTopic = "";       // Last worthy response - upcase and padded
    this.sTopic = "";       // Last worthy response - user input
    this.wPrevious = "";    // So we can check for repeats
    this.greet = false;
    this.last = [];         // To avoid answer repeat if possible
}

Eliza.prototype = new Object;

// Phrases to match are uppercase and padded with spaces, responses follow
Eliza.rules = [
    // Conversation rules
    " CAN YOU ",
	"Don't you believe that I can {}?",
	"Perhaps you would like to be able to {}?",
	"You want me to be able to {}?",
    " CAN I ",
	"Perhaps you don't want to {}?",
	"Do you want to be able to {}?",
    " YOU ARE ",
    " YOU'RE ",
	"What makes you think I am {}?",
	"Does it please you to believe I am {}?",
	"Perhaps you would like to be {}?",
	"Do you sometimes wish you were {}?",
    " I DON'T ",
	"Don't you really {}?",
	"Why don't you {}?",
	"Do you wish to be able to {}?",
	"Does that trouble you?",
    " I FEEL ",
	"Tell me more about such feelings.",
	"Do you often feel {}?",
	"Do you enjoy feeling {}?",
    " WHY DON'T YOU ",
	"Do you really believe I don't {}?",
	"Perhaps in good time I will {}.",
	"Do you want me to {}?",
    " WHY CAN'T I ",
	"Do you think you should be able to {}?",
	"Why can't you {}?",
    " ARE YOU ",
	"Why are you interested in whether or not I am {}?",
	"Would you prefer if I were not {}?",
	"Perhaps in your fantasies I am {}?",
    " I CAN'T ",
	"How do you know you can't {}?",
	"Have you tried?",
	"Perhaps you can now {}?",
    " I AM ",
    " I'M ",
	"Did you come to me because you are {}?",
	"How long have you been {}?",
	"Do you believe it is normal to be {}?",
	"Do you enjoy being {}?",
    " YOU ",
	"We were discussing you, not me.",
	"Oh... {}?",
	"You're not really talking about me, are you?",
    " I WANT ",
	"What would it mean to you if you got {}?",
	"Why do you want {}?",
	"Suppose you got {}?",
	"What if you never got {}?",
	"I sometimes also want {}.",
    " WHAT ",
    " HOW ",
    " WHO ",
    " WHERE ",
    " WHEN ",
    " WHY ",
	"Why do you ask?",
	"Does that question interest you?",
	"What answer would please you the most?",
	"What do you think?",
	"Are such questions on your mind often?",
	"What is it that you really want to know?",
	"Have you asked anyone else?",
	"Have you asked such questions before?",
	"What else comes to mind when you ask that?",
    " NAME ",
	"Names don't interest me.",
	"I don't care about names, please go on.",
    " CAUSE ",
	"Is that the real reason?",
	"Don't any other reasons come to mind?",
	"Does that reason explain anything else?",
	"What other reasons might there be?",
    " SORRY ",
	"Please don't apologise!",
	"Apologies are not necessary.",
	"What feelings do you have when you apologise?",
	"Don't be so defensive!",
    " DREAM ",
	"What does that dream suggest to you?",
	"Do you dream often?",
	"What persons appear in your dreams?",
	"Are you disturbed by your dreams?",
    " HELLO ",
    " HI ",
	"How are you today.. What would you like to discuss?",
	"Hi, there, I'm {me}.",
    " MAYBE ",
	"You don't seem quite certain.",
	"Why the uncertain tone?",
	"Can't you be more positive?",
	"You aren't sure?",
	"Don't you know?",
    " NO ",
	"Are you saying no just to be negative?",
	"You are being a bit negative.",
	"Why not?",
	"Are you sure?",
	"Why no?",
    " YOUR ",
	"Why are you concerned about my {}?",
	"What about your own {}?",
    " ALWAYS ",
	"Can you think of a specific example?",
	"When?",
	"What are you thinking of?",
	"Really, always?",
    " THINK ",
	"Do you really think so?",
	"But you are not sure you {}?",
	"Do you doubt you {}?",
    " ALIKE ",
	"In what way?",
	"What resemblence do you see?",
	"What does the similarity suggest to you?",
	"What other connections do you see?",
	"Could there really be some connection?",
	"How?",
	"You seem quite positive.",
    " YES ",
	"Are you Sure?",
	"I see.",
	"I understand.",
    " LET'S ",
	"Are you sure we should {}?",
	"Why do you think we should {}?",
	"Maybe we will {} some other time.",
    " FRIEND ",
    " FRIENDS ",
	"Why do you bring up the topic of friends?",
	"Do your friends worry you?",
	"Do your friends pick on you?",
	"Are you sure you have any friends?",
	"Do you impose on your friends?",
	"Perhaps your love for friends worries you.",
    " COMPUTER ",
    " COMPUTERS ",
	"Do computers worry you?",
	"Are you talking about me in particular?",
	"Are you frightened by machines?",
	"Why do you mention computers?",
	"What do you think machines have to do with your problems?",
        "Don't you think computers can help people?",
	"What is it about machines that worries you?",
    " OMELETTE ",
	"Are you confused about my accent?",
	"Does {me} sound French to you?",
	"Cuisine is not my strong point.",
	"I am not a chef to prepare omelette.",
    // Special rules used internally
    " **NOTFOUND** ",
	"Say, do you have any psychological problems?",
	"What does that suggest to you?",
	"I see.",
	"I'm not sure I understand you fully.",
	"Come, come, elucidate your thoughts.",
	"Can you elaborate on that?",
	"That is quite interesting.",
    " **REPEAT** ",
	"Why did you repeat yourself?",
	"Do you expect a different answer by repeating yourself?",
	"Come, come, elucidate your thoughts.",
	"Please don't repeat yourself!",
    " **VERSION** ",
	"ELIZA - Javascript Version by George Dunlop ( george@peccavi.com )"
];

// Punctuation separators
Eliza.punct = [ ".", ",", "!", "?", ":", ";", "&", '"', "@", "#", "(", ")" ];

// Array to convert between 1st and 2nd person in phrases
Eliza.conj1 = [
    [ "are",      "am"       ],
    [ "am",       "are"      ],
    [ "were",     "was"      ],
    [ "was",      "were"     ],
    [ "I",        "you"      ],
    [ "me",       "you"      ],
    [ "you",      "me"       ],
    [ "my",       "your"     ],
    [ "your",     "my"       ],
    [ "mine",     "your's"   ],
    [ "your's",   "mine"     ],
    [ "I'm",      "you're"   ],
    [ "you're",   "I'm"      ],
    [ "I've",     "you've"   ],
    [ "you've",   "I've"     ],
    [ "I'll",     "you'll"   ],
    [ "you'll",   "I'll"     ],
    [ "myself",   "yourself" ],
    [ "yourself", "myself"   ]
];

// Array to post process correct our tenses of pronouns such as "I/me"
Eliza.conj2 = [
    [ "me am",   "I am"   ],
    [ "am me",   "am I"   ],
    [ "me can",  "I can"  ],
    [ "can me",  "can I"  ],
    [ "me have", "I have" ],
    [ "me will", "I will" ],
    [ "will me", "will I" ]
];

// Function to replaces all occurances of substring substr1 with substr2 within strng
// if type == 0 straight string replacement
// if type == 1 assumes padded strings and replaces whole words only
// if type == 2 non case sensitive assumes padded strings to compare whole word only
// if type == 3 non case sensitive straight string replacement
Eliza.replaceStr = function(strng, substr1, substr2, type)
{
    var pntr = -1;
    switch (type) {
	case 0:
	    pntr = strng.indexOf(substr1);
	    break;
	case 1:
	    pntr = strng.indexOf(" " + substr1 + " ");
	    if (pntr >= 0)
		pntr++;
	    break;
	case 2:
	    var bstrng = strng.toUpperCase();
	    var bsubstr1 = substr1.toUpperCase();
	    pntr = bstrng.indexOf(" " + bsubstr1 + " ");
	    if (pntr >= 0)
		pntr++;
	    break;
	default:
	    var bstrng = strng.toUpperCase();
	    var bsubstr1 = substr1.toUpperCase();
	    pntr = bstrng.indexOf(bsubstr1);
    }
    if (pntr < 0)
	return strng;
    return strng.substr(0, pntr) + substr2 +
	Eliza.replaceStr(strng.substr(pntr + substr1.length), substr1, substr2, type);
};


// Function to pad a string, head, tail & punctuation. Static
Eliza.padString = function(strng)
{
    var aString = " " + strng + " ";
    for (var i = 0; i < Eliza.punct.length; i++)
	aString = Eliza.replaceStr(aString, Eliza.punct[i], " " + Eliza.punct[i] + " ", 0);
    return aString;
};

// Function to strip padding and compress embedded spaces. Static
Eliza.unpadString = function(strng)
{
    for (;;) {
	var aString = Eliza.replaceStr(strng, "  ", " ", 0); // compress spaces
	if (aString == strng)
	    break;
	strng = aString;
    }
    if (strng.charAt(0) == " ")
	aString = aString.substr(1);
    if (strng.charAt(aString.length - 1) == " ")
	aString = aString.substr(0, aString.length - 1);
    for (var i = 0; i < Eliza.punct.length; i++ )
	aString = Eliza.replaceStr(aString, " " + Eliza.punct[i], Eliza.punct[i], 0);
    return aString;
};

// Dress Input formatting i.e leading & trailing spaces and tail punctuation
Eliza.strTrim = function(strng)
{
    for (var end = strng.length - 1; end > 0; end--) {
	var c = strng.charAt(end);
	if (c == " ")
	    continue;
	var flg = false;
	for (var i = 0; i < Eliza.punct.length; i++) {
	    if (c == Eliza.punct[i]) {
		flg = true;
		break;
	    }
	}
	if (!flg)
	    break;
    }
    for (var begin = 0; begin <= end; begin++) {
	var c = strng.charAt(begin);
	if (c == " ")
	    continue;
	var flg = false;
	for (var i = 0; i < Eliza.punct.length; i++) {
	    if (c == Eliza.punct[i]) {
		flg = true;
		break;
	    }
	}
	if (!flg)
	    break;
    }
    return strng.substr(begin,end - begin + 1);
};

// Adjust pronouns and verbs & such. Static
Eliza.conjugate = function(sString)
{
    for (var i = 0; i < Eliza.conj1.length; i++)  // decompose
	sString = Eliza.replaceStr(sString, Eliza.conj1[i][0], "#@&" + i, 2);
    for (i = 0; i < Eliza.conj1.length; i++)      // recompose
	sString = Eliza.replaceStr(sString, "#@&" + i, Eliza.conj1[i][1], 2);
    // post process the resulting string
    for (i = 0; i < Eliza.conj2.length; i++)      // decompose
	sString = Eliza.replaceStr(sString, Eliza.conj2[i][0], "#@&" + i, 2);
    for (i = 0; i < Eliza.conj2.length; i++)      // recompose
	sString = Eliza.replaceStr(sString, "#@&" + i, Eliza.conj2[i][1], 2);
    return sString;
};

// Returns array index of first key found. Static
Eliza.findkey = function(wString)
{
    for (var i = 0; i < Eliza.rules.length; i++) {
	var r = Eliza.rules[i];
	if (r.charAt(0) != " ")
	    continue;
	if (wString.indexOf(r) >= 0)
	    return i;
    }
    return -1;
};

// Build our response string
// get a random choice of response based on the key
// Then structure the response
Eliza.prototype.phrase = function(sString, keyidx)
{
    var res = keyidx + 1;
    for (; res < Eliza.rules.length; res++) {
	var r = Eliza.rules[res];
	if (r.charAt(0) != " ")
	    break;
    }
    var len = res;
    for (; len < Eliza.rules.length; len++) {
	var r = Eliza.rules[len];
	if (r.charAt(0) == " ")
	    break;
    }
    len -= res;
    if (len > 1) {
	var choice = Math.random(0,len);
	while ((len > 2) && (this.last[keyidx] == choice))
	    choice = Math.random(0,len);
	this.last[keyidx] = choice;
	res += choice;
    }
    var rTemp = Eliza.rules[res];
    if (rTemp.indexOf("{") >= 0) {
	var sTemp = Eliza.padString(sString);
	var wTemp = sTemp.toUpperCase();
	var wWord = Eliza.rules[keyidx];
	var strpstr = wTemp.indexOf(wWord);
	strpstr += wWord.length - 1;
	var thisstr = Eliza.conjugate(sTemp.substr(strpstr));
	thisstr = Eliza.strTrim(Eliza.unpadString(thisstr));
	rTemp = Eliza.replaceStr(rTemp, "{me}", this.name, 0);
	rTemp = Eliza.replaceStr(rTemp, "{}", thisstr, 0);
    }
    return rTemp;
};

// This is the entry point and the I/O of this code
// Expects user input, returns response
Eliza.prototype.listen = function(User)
{
    var sInput = Eliza.strTrim(User);         // dress input formating
    if (sInput == "")
	return "I can't help, if you will not chat with me!";

    var wInput = Eliza.padString(sInput.toUpperCase()); // Work copy
    var foundkey = -1;
    if (wInput != this.wPrevious)             // check if user repeats himself
	foundkey = Eliza.findkey(wInput);
    else
	foundkey = Eliza.findkey(" **REPEAT** ");
    if (foundkey < 0) {
	if (!this.greet) {
	    this.greet = true;
	    return "Don't you ever say Hello?";
	}
	this.wPrevious = wInput;              // save input to check repeats
	if ((sInput.length < 10) && (this.wTopic != "") && (this.wTopic != this.wPrevious)) {
	    var lTopic = Eliza.conjugate(this.sTopic);
	    this.sTopic = "";
	    this.wTopic = "";
	    return 'OK... "' + lTopic + '". Tell me more.';
	} else {
	    if (sInput.length < 15)
		return "Tell me more...";
	    return this.phrase(sInput, Eliza.findkey(" **NOTFOUND** "));
	}
    } else {
	if (sInput.length > 12) {
	    this.sTopic = sInput;
	    this.wTopic = wInput;
	}
	this.greet = true;
	this.wPrevious = wInput;              // save input to check repeats
	return this.phrase(sInput, foundkey); // Get our response
    }
};

/* vi: set ts=8 sw=4 sts=4 noet: */
