/*
This Source Code Form is subject to the
terms of the Mozilla Public License, v.
2.0. If a copy of the MPL was not
distributed with this file, You can
obtain one at
http://mozilla.org/MPL/2.0/.
*/

/*
    MaSzyna EU07 locomotive simulator
    Copyright (C) 2001-2004  Marcin Wozniak and others

*/

#include "stdafx.h"
#include "EvLaunch.h"
#include "Globals.h"
#include "Logs.h"
#include "Usefull.h"
#include "McZapkie/mctools.h"
#include "Event.h"
#include "MemCell.h"
#include "mtable.h"
#include "Timer.h"
#include "parser.h"
#include "Console.h"

using namespace Mtable;

//---------------------------------------------------------------------------

TEventLauncher::TEventLauncher()
{ // ustawienie początkowych wartości dla wszystkich zmiennych
    iKey = 0;
    DeltaTime = -1;
    UpdatedTime = 0;
    fVal1 = fVal2 = 0;
#ifdef EU07_USE_OLD_TEVENTLAUNCHER_TEXT_ARRAY
    szText = NULL;
#endif
    iHour = iMinute = -1; // takiego czasu nigdy nie będzie
    dRadius = 0;
    Event1 = Event2 = NULL;
    MemCell = NULL;
    iCheckMask = 0;
}
#ifdef EU07_USE_OLD_TEVENTLAUNCHER_TEXT_ARRAY
TEventLauncher::~TEventLauncher()
{
    SafeDeleteArray(szText);
}
#endif
void TEventLauncher::Init()
{
}

bool TEventLauncher::Load(cParser *parser)
{ // wczytanie wyzwalacza zdarzeń
    std::string token;
    parser->getTokens();
    *parser >> dRadius; // promień działania
    if (dRadius > 0.0)
        dRadius *= dRadius; // do kwadratu, pod warunkiem, że nie jest ujemne
    parser->getTokens(); // klawisz sterujący
    *parser >> token;
    if (token != "none")
    {
        if (token.length() == 1)
            iKey = VkKeyScan(token[1]); // jeden znak jest konwertowany na kod klawisza
        else
            iKey = stol_def(token,0); // a jak więcej, to jakby numer klawisza jest
    }
    parser->getTokens();
    *parser >> DeltaTime;
    if (DeltaTime < 0)
        DeltaTime = -DeltaTime; // dla ujemnego zmieniamy na dodatni
    else if (DeltaTime > 0)
    { // wartość dodatnia oznacza wyzwalanie o określonej godzinie
        iMinute = int(DeltaTime) % 100; // minuty są najmłodszymi cyframi dziesietnymi
        iHour = int(DeltaTime - iMinute) / 100; // godzina to setki
        DeltaTime = 0; // bez powtórzeń
        WriteLog("EventLauncher at " + std::to_string(iHour) + ":" +
                 std::to_string(iMinute)); // wyświetlenie czasu
    }
    parser->getTokens();
    *parser >> token;
    asEvent1Name = token; // pierwszy event
    parser->getTokens();
    *parser >> token;
    asEvent2Name = token; // drugi event
    if ((asEvent2Name == "end") || (asEvent2Name == "condition"))
    { // drugiego eventu może nie być, bo są z tym problemy, ale ciii...
		token = asEvent2Name; // rozpoznane słowo idzie do dalszego przetwarzania
        asEvent2Name = "none"; // a drugiego eventu nie ma
    }
    else
    { // gdy są dwa eventy
        parser->getTokens();
        *parser >> token;
        //str = AnsiString(token.c_str());
    }
    if (token == "condition")
    { // obsługa wyzwalania warunkowego
        parser->getTokens();
        *parser >> token;
		asMemCellName = token;
        parser->getTokens();
        *parser >> token;
#ifdef EU07_USE_OLD_TEVENTLAUNCHER_TEXT_ARRAY
        SafeDeleteArray(szText);
        szText = new char[256];
        strcpy(szText, token.c_str());
#else
        szText = token;
#endif
        if (token != "*") //*=nie brać command pod uwagę
            iCheckMask |= conditional_memstring;
        parser->getTokens();
        *parser >> token;
        if (token != "*") //*=nie brać wartości 1. pod uwagę
        {
            iCheckMask |= conditional_memval1;
            fVal1 = atof(token.c_str());
        }
        else
            fVal1 = 0;
        parser->getTokens();
        *parser >> token;
        if (token.compare("*") != 0) //*=nie brać wartości 2. pod uwagę
        {
            iCheckMask |= conditional_memval2;
            fVal2 = atof(token.c_str());
        }
        else
            fVal2 = 0;
        parser->getTokens(); // słowo zamykające
        *parser >> token;
    }
    return true;
};

bool TEventLauncher::Render()
{ //"renderowanie" wyzwalacza
    bool bCond = false;
    if (iKey != 0)
    {
        if (Global::bActive) // tylko jeśli okno jest aktywne
            bCond = (Console::Pressed(iKey)); // czy klawisz wciśnięty
    }
    if (DeltaTime > 0)
    {
        if (UpdatedTime > DeltaTime)
        {
            UpdatedTime = 0; // naliczanie od nowa
            bCond = true;
        }
        else
            UpdatedTime += Timer::GetDeltaTime(); // aktualizacja naliczania czasu
    }
    else
    { // jeśli nie cykliczny, to sprawdzić czas
        if (GlobalTime->hh == iHour)
        {
            if (GlobalTime->mm == iMinute)
            { // zgodność czasu uruchomienia
                if (UpdatedTime < 10)
                {
                    UpdatedTime = 20; // czas do kolejnego wyzwolenia?
                    bCond = true;
                }
            }
        }
        else
            UpdatedTime = 1;
    }
    if (bCond) // jeśli spełniony został warunek
    {
        if ((iCheckMask != 0) && MemCell) // sprawdzanie warunku na komórce pamięci
            bCond = MemCell->Compare(szText, fVal1, fVal2, iCheckMask);
    }
    return bCond; // sprawdzanie dRadius w Ground.cpp
}

bool TEventLauncher::IsGlobal()
{ // sprawdzenie, czy jest globalnym wyzwalaczem czasu
    if (DeltaTime == 0)
        if (iHour >= 0)
            if (iMinute >= 0)
                if (dRadius < 0.0) // bez ograniczenia zasięgu
                    return true;
    return false;
};
//---------------------------------------------------------------------------
