// cg_text.c --

// this line must stay at top so the whole PCH thing works...
#include "cg_headers.h"

#include "cg_media.h"
#include "cg_text_precache.h"
#include "../game/speakers.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif


int precacheWav_i;	// Current high index of precacheWav array
precacheWav_t precacheWav[MAX_PRECACHEWAV];


int precacheText_i;	// Current high index of precacheText array
precacheText_t precacheText[MAX_PRECACHETEXT];
#define TEXT_SCREEN_WIDTH_FRACTION_CUTSCENE 0.85
#define TEXT_SCREEN_WIDTH_FRACTION_INGAME 0.85
#define TEXT_SCREEN_HEIGHT_INGAME 0.15f
#define TEXT_CUTSCENE_Y_BOOST 18
#define NUDGE_PERCENTAGE 0.90

extern vec4_t textcolor_caption;
extern vec4_t textcolor_center;
extern vec4_t textcolor_scroll;
#define	GAMETEXT_X_START	75.0f
#define	GAMETEXT_X_END		600.0f
#define	MAX_NUM_GAMELINES	4

void CG_GameText( int y, int charWidth )
{
	char	*s, *holds;
	int		i, len;
	float	x, w;
	int		numChars;
	int		text_i;
	char	str[MAX_QPATH];
	int		holdCnt, playingTime;
	int		totalLength, sound, max;

	Q_strncpyz( str, CG_Argv( 1 ), MAX_QPATH );
	cg.gameTextSpeaker = atoi( CG_Argv( 2 ) );
	cg.gameTextEntNum = atoi( CG_Argv( 3 ) );
	sound = cgs.sound_precache[atoi( CG_Argv( 4 ) )];

	text_i = CG_SearchTextPrecache( str );
	if ( text_i == -1 )
	{
		Com_Printf( "WARNING: CG_GameText given invalid text key :'%s'\n", str );
		return;
	}

	cg.gameTextTime = cg.time;
	cg.printTextY = 5 + SMALLCHAR_HEIGHT;
	cg.centerPrintCharWidth = charWidth;
	cg.gameTextCurrentLine = 0;
	cg.scrollTextLines = 1;
	memset( cg.printText, 0, sizeof( cg.printText ) );

	i = 0;
	len = 0;
	s = precacheText[text_i].text;
	holds = s;

	playingTime = cgi_S_GetSampleLength( sound );
	totalLength = strlen( s );
	if ( totalLength == 0 )
	{
		totalLength = 1;
	}
	cg.gameLetterTime = playingTime / totalLength;

	x = GAMETEXT_X_START;
	w = GAMETEXT_X_END - GAMETEXT_X_START;
	numChars = floor( w / SMALLCHAR_WIDTH );

	while ( *s )
	{
		len++;
		if ( *s == '\n' )
		{
			Q_strncpyz( cg.printText[i], holds, len );
			i++;
			len = 0;
			holds = s;
			holds++;
			cg.scrollTextLines++;
		}
		else if ( len == numChars )
		{
			while ( len && *s != ' ' )
			{
				s--;
				len--;
			}
			Q_strncpyz( cg.printText[i], holds, len );
			i++;
			len = 0;
			holds = s;
			holds++;
			cg.scrollTextLines++;
		}
		s++;
	}

	len++;
	Q_strncpyz( cg.printText[i], holds, len );

	max = MAX_NUM_GAMELINES;
	if ( max > cg.scrollTextLines )
	{
		max = cg.scrollTextLines;
	}

	holdCnt = 0;
	for ( i = 0; i < max; ++i )
	{
		holdCnt += strlen( cg.printText[i] );
	}
	cg.gameNextTextTime = cg.time + ( holdCnt * cg.gameLetterTime );
	cg.scrollTextTime = 0;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: CG_GameText ready key='%s' speaker=%d ent=%d sound=%d lines=%d first='%.64s'",
		str, cg.gameTextSpeaker, cg.gameTextEntNum, sound, cg.scrollTextLines, cg.printText[0]);
#endif
}

void CG_DrawGameText( void )
{
	char	*start;
	int		l;
	int		i, max;
	int		x, y;
	char	linebuffer[1024], string[1024];
	int		holdCnt;
	vec4_t	color;

	if ( !cg.gameTextTime )
	{
		return;
	}

	if ( cg.gameNextTextTime < cg.time )
	{
		cg.gameTextCurrentLine += MAX_NUM_GAMELINES;
		if ( cg.gameTextCurrentLine >= cg.scrollTextLines )
		{
			cg.gameTextTime = 0;
			return;
		}

		max = MAX_NUM_GAMELINES;
		if ( ( cg.scrollTextLines - cg.gameTextCurrentLine ) < max )
		{
			max = cg.scrollTextLines - cg.gameTextCurrentLine;
		}

		holdCnt = 0;
		for ( i = cg.gameTextCurrentLine; i < ( cg.gameTextCurrentLine + max ); ++i )
		{
			holdCnt += strlen( cg.printText[i] );
		}
		cg.gameNextTextTime = cg.time + ( holdCnt * cg.gameLetterTime );
	}

	if ( ( textcolor_caption[0] == 0 ) && ( textcolor_caption[1] == 0 ) &&
		( textcolor_caption[2] == 0 ) && ( textcolor_caption[3] == 0 ) )
	{
		Vector4Copy( colorTable[CT_WHITE], textcolor_caption );
	}

	color[0] = colorTable[CT_BLACK][0];
	color[1] = colorTable[CT_BLACK][1];
	color[2] = colorTable[CT_BLACK][2];
	color[3] = 0.350f;

	y = cg.printTextY;
	x = GAMETEXT_X_START;

	cgi_R_SetColor( color );
	CG_DrawPic( x - 4, y - SMALLCHAR_HEIGHT - 2, ( 70 * SMALLCHAR_WIDTH ),
		( ( ( ( cg.scrollTextLines > MAX_NUM_GAMELINES ) ? MAX_NUM_GAMELINES : cg.scrollTextLines ) + 1 ) * SMALLCHAR_HEIGHT ) + 4,
		cgs.media.ammoslider );

	if ( cg.gameTextSpeaker >= 0 && cg.gameTextSpeaker < SP_MAX )
	{
		sprintf( string, "%s:", speakerTable[cg.gameTextSpeaker].stringID );
	}
	else
	{
		sprintf( string, "VOICE:" );
	}
	CG_DrawStringExt( x, y - SMALLCHAR_HEIGHT, string, colorTable[CT_LTPURPLE1], qfalse, qtrue, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT );

	for ( i = cg.gameTextCurrentLine; i < cg.gameTextCurrentLine + MAX_NUM_GAMELINES; ++i )
	{
		start = cg.printText[i];
		while ( 1 )
		{
			for ( l = 0; l < 80; l++ )
			{
				if ( !start[l] || start[l] == '\n' )
				{
					break;
				}
				linebuffer[l] = start[l];
			}
			linebuffer[l] = 0;

			CG_DrawStringExt( x, y, linebuffer, textcolor_caption, qfalse, qtrue, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT );
			y += SMALLCHAR_HEIGHT;

			while ( *start && ( *start != '\n' ) )
			{
				start++;
			}
			if ( !*start )
			{
				break;
			}
			start++;
		}
	}

	cgi_R_SetColor( NULL );
}


// display text in a supplied box, start at top left and going down by however many pixels I feel like internally,
//	return value is NULL if all fitted, else char * of next char to continue from that didn't fit.
//
// (coords are in the usual 640x480 virtual space)...
//
// ( if you get the same char * returned as what you passed in, then none of it fitted at all (box too small) )
//
		// this is execrable, and should NOT have had to've been done now, but...
		//
		float gfAdvanceHack = 0.0f;	// MUST default to this
		int giLinesOutput;		// hack-city after release, only used by one function
//
const char *CG_DisplayBoxedText(int iBoxX, int iBoxY, int iBoxWidth, int iBoxHeight,
								const char *psText, int iFontHandle, float fScale,
								const vec4_t v4Color)
{
	giLinesOutput = 0;
	cgi_R_SetColor( v4Color );

	// Setup a reasonable vertical spacing (taiwanese & japanese need 1.5 fontheight, so use that for all)...
	//
	const int iFontHeight		 = cgi_R_Font_HeightPixels(iFontHandle, fScale);
	const int iFontHeightAdvance = (int) ( ((gfAdvanceHack == 0.0f) ? 1.5f : gfAdvanceHack) * (float) iFontHeight);
	int iYpos = iBoxY;	// start print pos

	// this could probably be simplified now, but it was converted from something else I didn't originally write,
	//	and it works anyway so wtf...
	//
	const char *psCurrentTextReadPos = psText;
	const char *psReadPosAtLineStart = psCurrentTextReadPos;
	const char *psBestLineBreakSrcPos = psCurrentTextReadPos;
	const char *psLastGood_s;	// needed if we get a full screen of chars with no punctuation or space (see usage notes)
	while( *psCurrentTextReadPos && (iYpos + iFontHeight < (iBoxY + iBoxHeight)) )
	{
		char sLineForDisplay[2048];	// ott

		// construct a line...
		//
		psCurrentTextReadPos = psReadPosAtLineStart;
		sLineForDisplay[0] = '\0';
		while ( *psCurrentTextReadPos )
		{
			psLastGood_s = psCurrentTextReadPos;

			// read letter...
			//
			qboolean bIsTrailingPunctuation;
			int iAdvanceCount;
			unsigned int uiLetter = cgi_AnyLanguage_ReadCharFromString(psCurrentTextReadPos, &iAdvanceCount, &bIsTrailingPunctuation);
			psCurrentTextReadPos += iAdvanceCount;

			// concat onto string so far...
			//
			if (uiLetter == 32 && sLineForDisplay[0] == '\0')
			{
				psReadPosAtLineStart++;
				continue;	// unless it's a space at the start of a line, in which case ignore it.
			}

			if (uiLetter > 255)
			{
				Q_strcat(sLineForDisplay, sizeof(sLineForDisplay),va("%c%c",uiLetter >> 8, uiLetter & 0xFF));
			}
			else
			{
				Q_strcat(sLineForDisplay, sizeof(sLineForDisplay),va("%c",uiLetter & 0xFF));
			}

			if (uiLetter == '\n')
			{
				// explicit new line...
				//
				sLineForDisplay[ strlen(sLineForDisplay)-1 ] = '\0';	// kill the CR
				psReadPosAtLineStart = psCurrentTextReadPos;
				psBestLineBreakSrcPos = psCurrentTextReadPos;
				break;	// print this line
			}
			else
			if ( cgi_R_Font_StrLenPixels(sLineForDisplay, iFontHandle, fScale) >= iBoxWidth )
			{
				// reached screen edge, so cap off string at bytepos after last good position...
				//
				if (uiLetter > 255 && bIsTrailingPunctuation && !cgi_Language_UsesSpaces())
				{
					// Special case, don't consider line breaking if you're on an asian punctuation char of
					//	a language that doesn't use spaces...
					//
				}
				else
				{
					if (psBestLineBreakSrcPos == psReadPosAtLineStart)
					{
						//  aarrrggh!!!!!   we'll only get here is someone has fed in a (probably) garbage string,
						//		since it doesn't have a single space or punctuation mark right the way across one line
						//		of the screen.  So far, this has only happened in testing when I hardwired a taiwanese
						//		string into this function while the game was running in english (which should NEVER happen
						//		normally).  On the other hand I suppose it'psCurrentTextReadPos entirely possible that some taiwanese string
						//		might have no punctuation at all, so...
						//
						psBestLineBreakSrcPos = psLastGood_s;	// force a break after last good letter
					}

					sLineForDisplay[ psBestLineBreakSrcPos - psReadPosAtLineStart ] = '\0';
					psReadPosAtLineStart = psCurrentTextReadPos = psBestLineBreakSrcPos;
					break;	// print this line
				}
			}

			// record last-good linebreak pos...  (ie if we've just concat'd a punctuation point (western or asian) or space)
			//
			if (bIsTrailingPunctuation || uiLetter == ' ' || (uiLetter > 255 && !cgi_Language_UsesSpaces()))
			{
				psBestLineBreakSrcPos = psCurrentTextReadPos;
			}
		}

		// ... and print it...
		//
		cgi_R_Font_DrawString(iBoxX, iYpos, sLineForDisplay, v4Color, iFontHandle, -1, fScale);
		iYpos += iFontHeightAdvance;
		giLinesOutput++;

		// and echo to console in dev mode...
		//
		if ( cg_developer.integer )
		{
//			Com_Printf( "%psCurrentTextReadPos\n", sLineForDisplay );
		}
	}
	return psReadPosAtLineStart;
}



/*
===============================================================================

CAPTION TEXT

===============================================================================
*/
void CG_CaptionTextStop(void)
{
	cg.captionTextTime = 0;
}

// try and get the correct StripEd text (with retry) for a given reference...
//
// returns 0 if failed, else strlen...
//
static int cg_SP_GetStringTextStringWithRetry( LPCSTR psReference, char *psDest, int iSizeofDest)
{
	int iReturn;

	if (psReference[0] == '#')
	{
		// then we know the striped package name is already built in, so do NOT try prepending anything else...
		//
		return cgi_SP_GetStringTextString( va("%s",psReference+1), psDest, iSizeofDest );
	}

	for (int i=0; i<STRIPED_LEVELNAME_VARIATIONS; i++)
	{
		if (cgs.stripLevelName[i][0])	// entry present?
		{
			iReturn = cgi_SP_GetStringTextString( va("%s_%s",cgs.stripLevelName[i],psReference), psDest, iSizeofDest );
			if (iReturn)
			{
				return iReturn;
			}
		}
	}

	return 0;
}

// slightly confusingly, the char arg for this function is an audio filename of the form "path/path/filename",
//	the "filename" part of which should be the same as the StripEd reference we're looking for in the current
//	level's string package...
//
void CG_CaptionText( const char *str, int sound, int y, int charWidth )
{
	const char	*s, *holds;
	int i;
	int	holdTime;
	char text[8192]={0};
	if ( str && str[0] == '@' )
	{
		int text_i = CG_SearchTextPrecache( (char *)str );
		int len = 0;
		int playingTime;
		int totalLength;

		if ( text_i == -1 )
		{
			Com_Printf( "WARNING: CG_CaptionText given invalid text key :'%s'\n", str );
			return;
		}

		cg.captionTextTime = cg.time;
		if ( in_camera )
		{
			cg.captionTextY = SCREEN_HEIGHT;
		}
		else
		{
			cg.captionTextY = SCREEN_HEIGHT - SMALLCHAR_HEIGHT * 3;
		}
		cg.centerPrintCharWidth = charWidth;
		cg.captionTextCurrentLine = 0;
		cg.scrollTextLines = 1;
		memset( cg.captionText, 0, sizeof( cg.captionText ) );

		i = 0;
		s = precacheText[text_i].text;
		holds = s;
		playingTime = cgi_S_GetSampleLength( sound );
		totalLength = strlen( s );
		if ( totalLength == 0 )
		{
			totalLength = 1;
		}
		cg.captionLetterTime = playingTime / totalLength;

		while ( *s )
		{
			Q_strcat( cg.captionText[i], sizeof( cg.captionText[i] ), va( "%c", *s ) );
			len++;
			if ( *s == '\n' )
			{
				Q_strncpyz( cg.captionText[i], holds, len );
				i++;
				len = 0;
				holds = s;
				holds++;
				cg.scrollTextLines++;
			}
			else if ( cgi_R_Font_StrLenPixels( cg.captionText[i], cgs.media.qhFontMedium, 1.0f ) >= SCREEN_WIDTH - ( cg.centerPrintCharWidth * 2 ) )
			{
				while ( len && *s != ' ' )
				{
					s--;
					len--;
				}
				Q_strncpyz( cg.captionText[i], holds, len );
				i++;
				len = 0;
				holds = s;
				holds++;
				cg.scrollTextLines++;
			}
			s++;
		}

		len++;
		Q_strncpyz( cg.captionText[i], holds, len );

		holdTime = strlen( cg.captionText[0] );
		if ( cg.scrollTextLines > 1 )
		{
			holdTime += strlen( cg.captionText[1] );
		}
		cg.captionNextTextTime = cg.time + ( holdTime * cg.captionLetterTime );
		cg.scrollTextTime = 0;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		XBLF("STEFX: CG_CaptionText ready key='%s' sound=%d lines=%d first='%.64s'",
			str, sound, cg.scrollTextLines, cg.captionText[0]);
#endif
		return;
	}

	const float fFontScale = cgi_Language_IsAsian() ? 0.8f : 1.0f;

	holds = strrchr(str,'/');
	if (!holds)
	{
#ifndef FINAL_BUILD
		Com_Printf("WARNING: CG_CaptionText given audio filename with no '/':'%s'\n",str);
#endif
		return;
	}
	i = cg_SP_GetStringTextStringWithRetry( holds+1, text, sizeof(text) );
	//ensure we found a match
	if (!i)
	{
#ifndef FINAL_BUILD
		// we only care about some sound dirs...
		if (!strnicmp(str,"sound/chars/",12))	// whichever language it is, it'll be pathed as english at this point
		{
			Com_Printf("WARNING: CG_CaptionText given invalid text key :'%s'\n",str);
		}
		else
		{
			// anything else is probably stuff we don't care about. It certainly shouldn't be speech, anyway
		}
#endif
		return;
	}

	const int fontHeight = (int) ((cgi_Language_IsAsian() ? 1.4f : 1.0f) * (float) cgi_R_Font_HeightPixels(cgs.media.qhFontMedium, fFontScale));	// taiwanese & japanese need 1.5 fontheight spacing
	int lineWidth;

	cg.captionTextTime = cg.time;
	if (in_camera) {
		cg.captionTextY = SCREEN_HEIGHT - (client_camera.bar_height_dest/2)- TEXT_CUTSCENE_Y_BOOST;	// ths is now a centre'd Y, not a start Y
#ifdef _XBOX
		if(cg.widescreen)
			lineWidth = 720 *  TEXT_SCREEN_WIDTH_FRACTION_CUTSCENE ;
		else
#endif
		lineWidth = SCREEN_WIDTH *  TEXT_SCREEN_WIDTH_FRACTION_CUTSCENE ;

	} else {	//get above the hud
		cg.captionTextY = (int) (TEXT_SCREEN_HEIGHT_INGAME * ((float)SCREEN_HEIGHT - (float)fontHeight * 1.5f));	// do NOT move this, it has to fit in between the weapon HUD and the datapad update.
#ifdef _XBOX
		if(cg.widescreen)
			lineWidth = 720 * TEXT_SCREEN_WIDTH_FRACTION_INGAME ;
		else
#endif
		lineWidth = SCREEN_WIDTH * TEXT_SCREEN_WIDTH_FRACTION_INGAME ;
	}
	cg.captionTextCurrentLine = 0;

	// count the number of lines for centering
	cg.scrollTextLines = 1;

	memset (cg.captionText, 0, sizeof(cg.captionText));

	// Break into individual lines
	i = 0;	// this could be completely removed and replace by "cg.scrollTextLines-1", but wtf?

	s=(const char*)&text;
	// tai...
//	s="賽卓哥爾博士已經安全了，我也把所有發現報告給「商店」。很不幸地，瑞士警局有些白癡發現了一些狀況，準備在機場逮捕亞歷西‧納克瑞得。他偽裝成外交使節，穿過了層層防備。現在他握有人質，並且威脅要散播病毒。根據最新的報告，納克瑞得以及他的黨羽已經完全佔據了機場。我受命來追捕納克瑞得以及救出所有人質。這並不容易。";
	// kor...
//	s="Wp:澗顫歜檜棻 詩萼. 斜菟檜 蜓и渠煎 啻陛 澀й雖 晦渠ж啊棻.澗顫歜檜棻 詩萼. 斜菟檜 蜓и渠煎 啻陛 澀й雖 晦渠ж啊棻.";
	holds = s;

	int iPlayingTimeMS	= cgi_S_GetSampleLength(sound);
	int iLengthInChars	= strlen(s);//cgi_R_Font_StrLenChars(s);	// strlen is also good for MBCS in this instance, since it's for timing
	if (iLengthInChars == 0)
	{
		iLengthInChars = 1;
	}
	cg.captionLetterTime = iPlayingTimeMS / iLengthInChars;

	const char *psBestLineBreakSrcPos = s;
	const char *psLastGood_s;	// needed if we get a full screen of chars with no punctuation or space (see usage notes)
	while( *s )
	{
		psLastGood_s = s;

		// read letter...
		//
		qboolean bIsTrailingPunctuation;
		int iAdvanceCount;
		unsigned int uiLetter = cgi_AnyLanguage_ReadCharFromString(s, &iAdvanceCount, &bIsTrailingPunctuation);
		s += iAdvanceCount;

		// concat onto string so far...
		//
		if (uiLetter == 32 && cg.captionText[i][0] == '\0')
		{
			holds++;
			continue;	// unless it's a space at the start of a line, in which case ignore it.
		}

		if (uiLetter > 255)
		{
			Q_strcat(cg.captionText[i],sizeof(cg.captionText[i]),va("%c%c",uiLetter >> 8, uiLetter & 0xFF));
		}
		else
		{
			Q_strcat(cg.captionText[i],sizeof(cg.captionText[i]),va("%c",uiLetter & 0xFF));
		}

		if (uiLetter == '\n')
		{
			// explicit new line...
			//
			cg.captionText[i][ strlen(cg.captionText[i])-1 ] = '\0';	// kill the CR
			i++;
			holds = s;
			psBestLineBreakSrcPos = s;
			cg.scrollTextLines++;
		}
		else
		if ( cgi_R_Font_StrLenPixels(cg.captionText[i], cgs.media.qhFontMedium, fFontScale) >= lineWidth)
		{
			// reached screen edge, so cap off string at bytepos after last good position...
			//
			if (uiLetter > 255 && bIsTrailingPunctuation && !cgi_Language_UsesSpaces())
			{
				// Special case, don't consider line breaking if you're on an asian punctuation char of
				//	a language that doesn't use spaces...
				//
			}
			else
			{
				if (psBestLineBreakSrcPos == holds)
				{
					//  aarrrggh!!!!!   we'll only get here is someone has fed in a (probably) garbage string,
					//		since it doesn't have a single space or punctuation mark right the way across one line
					//		of the screen.  So far, this has only happened in testing when I hardwired a taiwanese
					//		string into this function while the game was running in english (which should NEVER happen
					//		normally).  On the other hand I suppose it's entirely possible that some taiwanese string
					//		might have no punctuation at all, so...
					//
					psBestLineBreakSrcPos = psLastGood_s;	// force a break after last good letter
				}

				cg.captionText[i][ psBestLineBreakSrcPos - holds ] = '\0';
				holds = s = psBestLineBreakSrcPos;
				i++;
				cg.scrollTextLines++;
			}
		}

		// record last-good linebreak pos...  (ie if we've just concat'd a punctuation point (western or asian) or space)
		//
		if (bIsTrailingPunctuation || uiLetter == ' ' || (uiLetter > 255 && !cgi_Language_UsesSpaces()))
		{
			psBestLineBreakSrcPos = s;
		}
	}


	// calc the length of time to hold each 2 lines of text on the screen.... presumably this works?
	//
	holdTime = strlen(cg.captionText[0]);
	if (cg.scrollTextLines > 1)
	{
		holdTime += strlen(cg.captionText[1]);	// strlen is also good for MBCS in this instance, since it's for timing
	}
	cg.captionNextTextTime = cg.time + (/*JLF nudge it forward*/NUDGE_PERCENTAGE * holdTime * cg.captionLetterTime);

	cg.scrollTextTime = 0;	// No scrolling during captions

	//Echo to console in dev mode
	if ( cg_developer.integer )
	{
		Com_Printf( "%s\n", cg.captionText[0] );	// ste:  was [i], but surely sentence 0 is more useful than last?
	}
}


void CG_DrawCaptionText(void)
{
	int		i;
	int		x, y, w;
	int	holdTime;

	if ( !cg.captionTextTime )
	{
		return;
	}

	const float fFontScale = cgi_Language_IsAsian() ? 0.8f : 1.0f;

	if (cg_skippingcin.integer != 0)
	{
		cg.captionTextTime = 0;
		return;
	}

	if ( cg.captionNextTextTime < cg.time )
	{
		cg.captionTextCurrentLine += 2;

		if (cg.captionTextCurrentLine >= cg.scrollTextLines)
		{
			cg.captionTextTime = 0;
			return;
		}
		else
		{
			holdTime = strlen(cg.captionText[cg.captionTextCurrentLine]);
			if (cg.scrollTextLines >= cg.captionTextCurrentLine)
			{
				// ( strlen is also good for MBCS in this instance, since it's for timing -ste)
				//
				holdTime += strlen(cg.captionText[cg.captionTextCurrentLine + 1]);
			}

			cg.captionNextTextTime = cg.time + (holdTime * cg.captionLetterTime);//50);
		}
	}

	// Give a color if one wasn't given
	if((textcolor_caption[0] == 0) && (textcolor_caption[1] == 0) &&
		(textcolor_caption[2] == 0) && (textcolor_caption[3] == 0))
	{
		Vector4Copy( colorTable[CT_WHITE], textcolor_caption );
	}

	cgi_R_SetColor(textcolor_caption);

	// Set Y of the first line (varies if only printing one line of text)
	// (this all works, please don't mess with it)
	const int fontHeight = (int) ((cgi_Language_IsAsian() ? 1.4f : 1.0f) * (float) cgi_R_Font_HeightPixels(cgs.media.qhFontMedium, fFontScale));
	const bool bPrinting2Lines = !!(cg.captionText[ cg.captionTextCurrentLine+1 ][0]);
	y = cg.captionTextY - ( (float)fontHeight * (bPrinting2Lines ? 1 : 0.5f));	// captionTextY was a centered Y pos, not a top one
	y -= cgi_Language_IsAsian() ? 0 : 4;

	for (i=	cg.captionTextCurrentLine;i< cg.captionTextCurrentLine + 2;++i)
	{
		w = cgi_R_Font_StrLenPixels(cg.captionText[i], cgs.media.qhFontMedium, fFontScale);
		if (w)
		{
			x = (SCREEN_WIDTH-w) / 2;
			cgi_R_Font_DrawString(x, y, cg.captionText[i], textcolor_caption, cgs.media.qhFontMedium, -1, fFontScale);
			y += fontHeight;
		}
	}

	cgi_R_SetColor( NULL );
}

/*
===============================================================================

SCROLL TEXT

===============================================================================

CG_ScrollText - split text up into seperate lines

 'str' arg is StripEd string reference, eg "CREDITS_RAVEN"

*/
void CG_ScrollText( const char *str, int y, int charWidth )
{
	char	*s,*holds;
	int i, len, numChars;
	int text_i;

	// Find text to match the str given
	text_i = CG_SearchTextPrecache((char *) str);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: CG_ScrollText start key='%s' textIndex=%d precacheText=%d y=%d charWidth=%d color=(%g,%g,%g,%g)",
		str ? str : "(null)",
		text_i,
		precacheText_i,
		y,
		charWidth,
		textcolor_scroll[0], textcolor_scroll[1], textcolor_scroll[2], textcolor_scroll[3]);
#endif

	cg.scrollTextTime = cg.time;
	cg.printTextY = SCREEN_HEIGHT;

	cg.centerPrintCharWidth = charWidth;

	// count the number of lines for centering
	cg.scrollTextLines = 1;

	// Break text string into individual lines
	if (text_i == -1)	// Didn't find a match so just print what you were given
	{
		s = (char *) str;
	}
	else
	{
		s = precacheText[text_i].text;
	}

	i = 0;
	len = 0;
	holds = s;

	numChars = floor( (float)(SCREEN_WIDTH - (2*cg.centerPrintCharWidth)) / (float)cg.centerPrintCharWidth );

	while( *s )
	{
		len++;
		if (*s == '\n')
		{//Being told explicitly to start a new line
			Q_strncpyz( cg.printText[i], holds, len);
			i++;
			len = 0;
			holds = s;
			holds++;
			cg.scrollTextLines++;
		}
		else if ( len == numChars )
		{//Reached max length of this line
			//step back until we find a space
			while( len && *s != ' ' )
			{
				s--;
				len--;
			}
			Q_strncpyz( cg.printText[i], holds, len);
			i++;
			len = 0;
			holds = s;
			holds++;
			cg.scrollTextLines++;
			assert (i < (sizeof(cg.printText)/sizeof(cg.printText[0])) );
			if (i >= (sizeof(cg.printText)/sizeof(cg.printText[0])) )
			{
				break;
			}
		}
		s++;
	}

	// To get the last line
	len++;  // So the NULL will be properly placed at the end of the string of Q_strncpyz
	Q_strncpyz( cg.printText[i], holds, len);
	cg.captionTextTime = 0;		// No captions during scrolling
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: CG_ScrollText ready key='%s' lines=%d first='%.64s'",
		str ? str : "(null)",
		cg.scrollTextLines,
		cg.printText[0]);
#endif
}

#define SCROLL_LPM (1/50.0) // 1 line per 50 ms
void CG_DrawScrollText(void)
{
	char	*start;
	int		l;
	int		i;
	int		y;
	char linebuffer[1024];
	const int	pad = BIGCHAR_HEIGHT * 1.75;

	if ( !cg.scrollTextTime )
	{
		return;
	}

	if((textcolor_scroll[0] == 0) && (textcolor_scroll[1] == 0) &&
		(textcolor_scroll[2] == 0) && (textcolor_scroll[3] == 0))
	{
		Vector4Copy( colorTable[CT_WHITE], textcolor_scroll );
	}

	cgi_R_SetColor( textcolor_scroll );

	y = cg.printTextY - (cg.time - cg.scrollTextTime) * SCROLL_LPM;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{
		static int s_stefxScrollDrawBudget = 48;
		if ( s_stefxScrollDrawBudget > 0 )
		{
			XBLF("STEFX: CG_DrawScrollText active time=%d start=%d y=%d lines=%d color=(%g,%g,%g,%g) first='%.64s'",
				cg.time,
				cg.scrollTextTime,
				y,
				cg.scrollTextLines,
				textcolor_scroll[0], textcolor_scroll[1], textcolor_scroll[2], textcolor_scroll[3],
				cg.printText[0]);
			--s_stefxScrollDrawBudget;
		}
	}
#endif

	// See if text has finished scrolling off screen
	if ((y + cg.scrollTextLines * pad) < 1)
	{
		cg.scrollTextTime = 0;
		return;
	}

	for (i=0;i<cg.scrollTextLines;++i)
	{

		// Is this line off top of screen?
		if ((y + ((i +1) * pad)) < 1)
		{
			y += pad;
			continue;
		}
		// or past bottom of screen?
		else if (y > SCREEN_HEIGHT)
		{
			break;
		}

		start = cg.printText[i];

		while ( 1 )
		{

			for ( l = 0; l < 80; l++ )
			{
				if ( !start[l] || start[l] == '\n' )
				{
					break;
				}
				linebuffer[l] = start[l];
			}
			linebuffer[l] = 0;

			CG_DrawProportionalString(120, y, linebuffer, CG_BIGFONT | CG_DROPSHADOW, textcolor_scroll );

			y += pad;

			while ( *start && ( *start != '\n' ) )
			{
				start++;
			}

			if ( !*start )
			{
				break;
			}
			start++;
		}
	}

	cgi_R_SetColor( NULL );
}

int CG_SearchTextPrecache( char *key )
{
	int i;

	if ( !key || key[0] != '@' )
	{
		return -1;
	}

	for ( i = 0; i < precacheText_i; ++i )
	{
		if ( precacheText[i].key && !stricmp( key, precacheText[i].key ) )
		{
			return i;
		}
	}

	return -1;
}

int CG_SearchWavPrecache( char *key )
{
	int i;

	if ( !key || !key[0] )
	{
		return -1;
	}

	for ( i = 0; i < precacheWav_i; ++i )
	{
		if ( precacheWav[i].wavFile && !stricmp( key, precacheWav[i].wavFile ) )
		{
			return i;
		}
	}

	return -1;
}


/*
===============================================================================

LCARS TEXT

===============================================================================
*/
#define	LCARS_X_START	87
#define	LCARS_X_END		610

void CG_LCARSText( const char *str, int y, int charWidth )
{
	int text_i, i, len;
	char *s, *holds;
	int w, numChars;
	int holdTime;

	text_i = CG_SearchTextPrecache( (char *)str );
	if ( text_i == -1 )
	{
		Com_Printf( "WARNING: CG_LCARSText given invalid text key :'%s'\n", str );
		return;
	}

	cg.LCARSTextLines = 1;
	memset( cg.LCARSText, 0, sizeof( cg.LCARSText ) );

	i = 0;
	len = 0;
	s = precacheText[text_i].text;
	holds = s;

	w = LCARS_X_END - LCARS_X_START;
	numChars = floor( (float)w / (float)SMALLCHAR_WIDTH );

	while ( *s )
	{
		len++;
		if ( *s == '\n' )
		{
			Q_strncpyz( cg.LCARSText[i], holds, len );
			i++;
			len = 0;
			holds = s;
			holds++;
			cg.LCARSTextLines++;
		}
		else if ( len == numChars )
		{
			while ( len && *s != ' ' )
			{
				s--;
				len--;
			}
			Q_strncpyz( cg.LCARSText[i], holds, len );
			i++;
			len = 0;
			holds = s;
			holds++;
			cg.LCARSTextLines++;
		}
		s++;
	}

	len++;
	Q_strncpyz( cg.LCARSText[i], holds, len );

	holdTime = strlen( precacheText[text_i].text ) * 50;
	if ( holdTime < 5000 )
	{
		holdTime = 5000;
	}
	cg.LCARSTextTime = cg.time + holdTime;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: CG_LCARSText ready key='%s' lines=%d first='%.64s'",
		str, cg.LCARSTextLines, cg.LCARSText[0]);
#endif
}

void CG_DrawLCARSText( void )
{
	int i;
	int y, pad;

	if ( cg.LCARSTextTime < cg.time )
	{
		return;
	}

	pad = SMALLCHAR_HEIGHT * 1.15;
	y = ( ( SCREEN_HEIGHT / 2 ) - ( ( cg.LCARSTextLines * pad ) / 2 ) ) - ( 3 * pad );
	for ( i = 0; i < cg.LCARSTextLines; ++i )
	{
		CG_DrawProportionalString( ( SCREEN_WIDTH / 2 ), y, cg.LCARSText[i], CG_PULSE | CG_SMALLFONT | CG_CENTER, colorTable[CT_LTGOLD1] );
		y += pad;
	}
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/


/*
==============
CG_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void CG_CenterPrint( const char *str, int y) {
	char	*s;

	// Find text to match the str given
	if (*str == '@')
	{
		int i;

		i = cgi_SP_GetStringTextString( str+1, cg.centerPrint, sizeof(cg.centerPrint) );

		if (!i)
		{
			Com_Printf (S_COLOR_RED"CG_CenterPrint: cannot find reference '%s' in StringPackage!\n",str);
			Q_strncpyz( cg.centerPrint, str, sizeof(cg.centerPrint) );
		}
	}
	else
	{
		Q_strncpyz( cg.centerPrint, str, sizeof(cg.centerPrint) );
	}

	cg.centerPrintTime = cg.time;
	cg.centerPrintY = y;

	// count the number of lines for centering
	cg.centerPrintLines = 1;
	s = cg.centerPrint;
	while( *s ) {
		if (*s == '\n')
			cg.centerPrintLines++;
		s++;
	}

}


/*
===================
CG_DrawCenterString
===================
*/
void CG_DrawCenterString( void )
{
	char	*start;
	int		l;
	int		x, y, w;
	float	*color;

	if ( !cg.centerPrintTime ) {
		return;
	}

	color = CG_FadeColor( cg.centerPrintTime, 1000 * 3 );
	if ( !color ) {
		return;
	}

	if((textcolor_center[0] == 0) && (textcolor_center[1] == 0) &&
		(textcolor_center[2] == 0) && (textcolor_center[3] == 0))
	{
		Vector4Copy( colorTable[CT_WHITE], textcolor_center );
	}

	start = cg.centerPrint;

	const int fontHeight = cgi_R_Font_HeightPixels(cgs.media.qhFontMedium, 1.0f);
	y = cg.centerPrintY - (cg.centerPrintLines * fontHeight) / 2;

	while ( 1 ) {
		char linebuffer[1024];

		// this is kind of unpleasant when dealing with MBCS, but...
		//
		const char *psString = start;
		int iOutIndex = 0;
		for ( l = 0; l < sizeof(linebuffer)-1; l++ ) {
			int iAdvanceCount;
			unsigned int uiLetter = cgi_AnyLanguage_ReadCharFromString(psString, &iAdvanceCount);
			psString += iAdvanceCount;
			if (!uiLetter || uiLetter == '\n'){
				break;
			}
			if (uiLetter > 255)
			{
				linebuffer[iOutIndex++] = uiLetter >> 8;
				linebuffer[iOutIndex++] = uiLetter & 0xFF;
			}
			else
			{
				linebuffer[iOutIndex++] = uiLetter & 0xFF;
			}
		}
		linebuffer[iOutIndex++] = '\0';

		w = cgi_R_Font_StrLenPixels(linebuffer, cgs.media.qhFontMedium, 1.0f);

		x = ( SCREEN_WIDTH - w ) / 2;

		cgi_R_Font_DrawString(x,y,linebuffer, textcolor_center, cgs.media.qhFontMedium, -1, 1.0f);

		y += fontHeight;

		while ( *start && ( *start != '\n' ) ) {
			start++;
		}
		if ( !*start ) {
			break;
		}
		start++;
	}

}
