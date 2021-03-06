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

#include "system.hpp"
#include "classes.hpp"

#include "opengl/glew.h"
#include "opengl/glut.h"

#pragma hdrstop

#include "Timer.h"
#include "Texture.h"
#include "Ground.h"
#include "Globals.h"
#include "Event.h"
#include "EvLaunch.h"
#include "TractionPower.h"
#include "Traction.h"
#include "Track.h"
#include "RealSound.h"
#include "AnimModel.h"
#include "MemCell.h"
#include "mtable.hpp"
#include "DynObj.h"
#include "Data.h"
#include "parser.h" //Tolaris-010603
#include "Driver.h"
#include "Console.h"
#include "Names.h"

#define _PROBLEND 1
//---------------------------------------------------------------------------
#pragma package(smart_init)

bool bCondition; // McZapkie: do testowania warunku na event multiple
AnsiString LogComment;

//---------------------------------------------------------------------------
// Obiekt renderuj�cy siatk� jest sztucznie tworzonym obiektem pomocniczym,
// grupuj�cym siatki obiekt�w dla danej tekstury. Obiektami sk�adowymi mog�
// byc tr�jk�ty terenu, szyny, podsypki, a tak�e proste modele np. s�upy.
// Obiekty sk�adowe dodane s� do listy TSubRect::nMeshed z list� zrobion� na
// TGroundNode::nNext3, gdzie s� posortowane wg tekstury. Obiekty renderuj�ce
// s� wpisane na list� TSubRect::nRootMesh (TGroundNode::nNext2) oraz na
// odpowiednie listy renderowania, gdzie zast�puj� obiekty sk�adowe (nNext3).
// Problematyczne s� tory/drogi/rzeki, gdzie u�ywane sa 2 tekstury. Dlatego
// tory s� zdublowane jako TP_TRACK oraz TP_DUMMYTRACK. Je�li tekstura jest
// tylko jedna (np. zwrotnice), nie jest u�ywany TP_DUMMYTRACK.
//---------------------------------------------------------------------------
TGroundNode::TGroundNode()
{ // nowy obiekt terenu - pusty
    iType = GL_POINTS;
    Vertices = NULL;
    nNext = nNext2 = NULL;
    pCenter = vector3(0, 0, 0);
    iCount = 0; // wierzcho�k�w w tr�jk�cie
    // iNumPts=0; //punkt�w w linii
    TextureID = 0;
    iFlags = 0; // tryb przezroczysto�ci nie zbadany
    DisplayListID = 0;
    Pointer = NULL; // zerowanie wska�nika kontekstowego
    bVisible = false; // czy widoczny
    fSquareRadius = 10000 * 10000;
    fSquareMinRadius = 0;
    asName = "";
    // Color= TMaterialColor(1);
    // fAngle=0; //obr�t dla modelu
    // fLineThickness=1.0; //mm dla linii
    for (int i = 0; i < 3; i++)
    {
        Ambient[i] = Global::whiteLight[i] * 255;
        Diffuse[i] = Global::whiteLight[i] * 255;
        Specular[i] = Global::noLight[i] * 255;
    }
    nNext3 = NULL; // nie wy�wietla innych
    iVboPtr = -1; // indeks w VBO sektora (-1: nie u�ywa VBO)
    iVersion = 0; // wersja siatki
}

TGroundNode::~TGroundNode()
{
    // if (iFlags&0x200) //czy obiekt zosta� utworzony?
    switch (iType)
    {
    case TP_MEMCELL:
        SafeDelete(MemCell);
        break;
    case TP_EVLAUNCH:
        SafeDelete(EvLaunch);
        break;
    case TP_TRACTION:
        SafeDelete(hvTraction);
        break;
    case TP_TRACTIONPOWERSOURCE:
        SafeDelete(psTractionPowerSource);
        break;
    case TP_TRACK:
        SafeDelete(pTrack);
        break;
    case TP_DYNAMIC:
        SafeDelete(DynamicObject);
        break;
    case TP_MODEL:
        if (iFlags & 0x200) // czy model zosta� utworzony?
            delete Model;
        Model = NULL;
        break;
    case TP_TERRAIN:
    { // pierwsze nNode zawiera model E3D, reszta to tr�jk�ty
        for (int i = 1; i < iCount; ++i)
            nNode->Vertices =
                NULL; // zerowanie wska�nik�w w kolejnych elementach, bo nie s� do usuwania
        delete[] nNode; // usuni�cie tablicy i pierwszego elementu
    }
    case TP_SUBMODEL: // dla formalno�ci, nie wymaga usuwania
        break;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        SafeDeleteArray(Points);
        break;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_TRIANGLES:
        SafeDeleteArray(Vertices);
        break;
    }
}

void TGroundNode::Init(int n)
{ // utworzenie tablicy wierzcho�k�w
    bVisible = false;
    iNumVerts = n;
    Vertices = new TGroundVertex[iNumVerts];
}

TGroundNode::TGroundNode(TGroundNodeType t, int n)
{ // utworzenie obiektu
    TGroundNode(); // domy�lne ustawienia
    iNumVerts = n;
    if (iNumVerts)
        Vertices = new TGroundVertex[iNumVerts];
    iType = t;
    switch (iType)
    { // zale�nie od typu
    case TP_TRACK:
        pTrack = new TTrack(this);
        break;
    }
}

void TGroundNode::InitCenter()
{ // obliczenie �rodka ci�ko�ci obiektu
    for (int i = 0; i < iNumVerts; i++)
        pCenter += Vertices[i].Point;
    pCenter /= iNumVerts;
}

void TGroundNode::InitNormals()
{ // obliczenie wektor�w normalnych
    vector3 v1, v2, v3, v4, v5, n1, n2, n3, n4;
    int i;
    float tu, tv;
    switch (iType)
    {
    case GL_TRIANGLE_STRIP:
        v1 = Vertices[0].Point - Vertices[1].Point;
        v2 = Vertices[1].Point - Vertices[2].Point;
        n1 = SafeNormalize(CrossProduct(v1, v2));
        if (Vertices[0].Normal == vector3(0, 0, 0))
            Vertices[0].Normal = n1;
        v3 = Vertices[2].Point - Vertices[3].Point;
        n2 = SafeNormalize(CrossProduct(v3, v2));
        if (Vertices[1].Normal == vector3(0, 0, 0))
            Vertices[1].Normal = (n1 + n2) * 0.5;

        for (i = 2; i < iNumVerts - 2; i += 2)
        {
            v4 = Vertices[i - 1].Point - Vertices[i].Point;
            v5 = Vertices[i].Point - Vertices[i + 1].Point;
            n3 = SafeNormalize(CrossProduct(v3, v4));
            n4 = SafeNormalize(CrossProduct(v5, v4));
            if (Vertices[i].Normal == vector3(0, 0, 0))
                Vertices[i].Normal = (n1 + n2 + n3) / 3;
            if (Vertices[i + 1].Normal == vector3(0, 0, 0))
                Vertices[i + 1].Normal = (n2 + n3 + n4) / 3;
            n1 = n3;
            n2 = n4;
            v3 = v5;
        }
        if (Vertices[i].Normal == vector3(0, 0, 0))
            Vertices[i].Normal = (n1 + n2) / 2;
        if (Vertices[i + 1].Normal == vector3(0, 0, 0))
            Vertices[i + 1].Normal = n2;
        break;
    case GL_TRIANGLE_FAN:

        break;
    case GL_TRIANGLES:
        for (i = 0; i < iNumVerts; i += 3)
        {
            v1 = Vertices[i + 0].Point - Vertices[i + 1].Point;
            v2 = Vertices[i + 1].Point - Vertices[i + 2].Point;
            n1 = SafeNormalize(CrossProduct(v1, v2));
            if (Vertices[i + 0].Normal == vector3(0, 0, 0))
                Vertices[i + 0].Normal = (n1);
            if (Vertices[i + 1].Normal == vector3(0, 0, 0))
                Vertices[i + 1].Normal = (n1);
            if (Vertices[i + 2].Normal == vector3(0, 0, 0))
                Vertices[i + 2].Normal = (n1);
            tu = floor(Vertices[i + 0].tu);
            tv = floor(Vertices[i + 0].tv);
            Vertices[i + 1].tv -= tv;
            Vertices[i + 2].tv -= tv;
            Vertices[i + 0].tv -= tv;
            Vertices[i + 1].tu -= tu;
            Vertices[i + 2].tu -= tu;
            Vertices[i + 0].tu -= tu;
        }
        break;
    }
}

void TGroundNode::MoveMe(vector3 pPosition)
{ // przesuwanie obiekt�w scenerii o wektor w celu redukcji trz�sienia
    pCenter += pPosition;
    switch (iType)
    {
    case TP_TRACTION:
        hvTraction->pPoint1 += pPosition;
        hvTraction->pPoint2 += pPosition;
        hvTraction->pPoint3 += pPosition;
        hvTraction->pPoint4 += pPosition;
        hvTraction->Optimize();
        break;
    case TP_MODEL:
    case TP_DYNAMIC:
    case TP_MEMCELL:
    case TP_EVLAUNCH:
        break;
    case TP_TRACK:
        pTrack->MoveMe(pPosition);
        break;
    case TP_SOUND: // McZapkie - dzwiek zapetlony w zaleznosci od odleglosci
        tsStaticSound->vSoundPosition += pPosition;
        break;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        for (int i = 0; i < iNumPts; i++)
            Points[i] += pPosition;
        ResourceManager::Unregister(this);
        break;
    default:
        for (int i = 0; i < iNumVerts; i++)
            Vertices[i].Point += pPosition;
        ResourceManager::Unregister(this);
    }
}

void TGroundNode::RaRenderVBO()
{ // renderowanie z domyslnego bufora VBO
    glColor3ub(Diffuse[0], Diffuse[1], Diffuse[2]);
    if (TextureID)
        glBindTexture(GL_TEXTURE_2D, TextureID); // Ustaw aktywn� tekstur�
    glDrawArrays(iType, iVboPtr, iNumVerts); // Narysuj naraz wszystkie tr�jk�ty
}

void TGroundNode::RenderVBO()
{ // renderowanie obiektu z VBO - faza nieprzezroczystych
    double mgn = SquareMagnitude(pCenter - Global::pCameraPosition);
    if ((mgn > fSquareRadius || (mgn < fSquareMinRadius)) &&
        (iType != TP_EVLAUNCH)) // McZapkie-070602: nie rysuj odleglych obiektow ale sprawdzaj
        // wyzwalacz zdarzen
        return;
    int i, a;
    switch (iType)
    {
    case TP_TRACTION:
        return;
    case TP_TRACK:
        if (iNumVerts)
            pTrack->RaRenderVBO(iVboPtr);
        return;
    case TP_MODEL:
        Model->RenderVBO(&pCenter);
        return;
    // case TP_SOUND: //McZapkie - dzwiek zapetlony w zaleznosci od odleglosci
    // if ((pStaticSound->GetStatus()&DSBSTATUS_PLAYING)==DSBPLAY_LOOPING)
    // {
    //  pStaticSound->Play(1,DSBPLAY_LOOPING,true,pStaticSound->vSoundPosition);
    //  pStaticSound->AdjFreq(1.0,Timer::GetDeltaTime());
    // }
    // return; //Ra: TODO sprawdzi�, czy d�wi�ki nie s� tylko w RenderHidden
    case TP_MEMCELL:
        return;
    case TP_EVLAUNCH:
        if (EvLaunch->Render())
            if ((EvLaunch->dRadius < 0) || (mgn < EvLaunch->dRadius))
            {
                if (Console::Pressed(VK_SHIFT) && EvLaunch->Event2 != NULL)
                    Global::AddToQuery(EvLaunch->Event2, NULL);
                else if (EvLaunch->Event1 != NULL)
                    Global::AddToQuery(EvLaunch->Event1, NULL);
            }
        return;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        if (iNumPts)
        {
            float linealpha = 255000 * fLineThickness / (mgn + 1.0);
            if (linealpha > 255)
                linealpha = 255;
            float r, g, b;
            r = floor(Diffuse[0] * Global::ambientDayLight[0]); // w zaleznosci od koloru swiatla
            g = floor(Diffuse[1] * Global::ambientDayLight[1]);
            b = floor(Diffuse[2] * Global::ambientDayLight[2]);
            glColor4ub(r, g, b, linealpha); // przezroczystosc dalekiej linii
            // glDisable(GL_LIGHTING); //nie powinny �wieci�
            glDrawArrays(iType, iVboPtr, iNumPts); // rysowanie linii
            // glEnable(GL_LIGHTING);
        }
        return;
    default:
        if (iVboPtr >= 0)
            RaRenderVBO();
    };
    return;
};

void TGroundNode::RenderAlphaVBO()
{ // renderowanie obiektu z VBO - faza przezroczystych
    double mgn = SquareMagnitude(pCenter - Global::pCameraPosition);
    float r, g, b;
    if (mgn < fSquareMinRadius)
        return;
    if (mgn > fSquareRadius)
        return;
    int i, a;
#ifdef _PROBLEND
    if ((PROBLEND)) // sprawdza, czy w nazwie nie ma @    //Q: 13122011 - Szociu: 27012012
    {
        glDisable(GL_BLEND);
        glAlphaFunc(GL_GREATER, 0.45); // im mniejsza warto��, tym wi�ksza ramka, domy�lnie 0.1f
    };
#endif
    switch (iType)
    {
    case TP_TRACTION:
        if (bVisible)
        {
#ifdef _PROBLEND
            glEnable(GL_BLEND);
            glAlphaFunc(GL_GREATER, 0.04);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
            hvTraction->RenderVBO(mgn, iVboPtr);
        }
        return;
    case TP_MODEL:
#ifdef _PROBLEND
        glEnable(GL_BLEND);
        glAlphaFunc(GL_GREATER, 0.04);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
        Model->RenderAlphaVBO(&pCenter);
        return;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        if (iNumPts)
        {
            float linealpha = 255000 * fLineThickness / (mgn + 1.0);
            if (linealpha > 255)
                linealpha = 255;
            r = Diffuse[0] * Global::ambientDayLight[0]; // w zaleznosci od koloru swiatla
            g = Diffuse[1] * Global::ambientDayLight[1];
            b = Diffuse[2] * Global::ambientDayLight[2];
            glColor4ub(r, g, b, linealpha); // przezroczystosc dalekiej linii
            // glDisable(GL_LIGHTING); //nie powinny �wieci�
            glDrawArrays(iType, iVboPtr, iNumPts); // rysowanie linii
// glEnable(GL_LIGHTING);
#ifdef _PROBLEND
            glEnable(GL_BLEND);
            glAlphaFunc(GL_GREATER, 0.04);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
        }
#ifdef _PROBLEND
        glEnable(GL_BLEND);
        glAlphaFunc(GL_GREATER, 0.04);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
        return;
    default:
        if (iVboPtr >= 0)
        {
            RaRenderVBO();
#ifdef _PROBLEND
            glEnable(GL_BLEND);
            glAlphaFunc(GL_GREATER, 0.04);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
            return;
        }
    }
#ifdef _PROBLEND
    glEnable(GL_BLEND);
    glAlphaFunc(GL_GREATER, 0.04);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
    return;
}

void TGroundNode::Compile(bool many)
{ // tworzenie skompilowanej listy w wy�wietlaniu DL
    if (!many)
    { // obs�uga pojedynczej listy
        if (DisplayListID)
            Release();
        if (Global::bManageNodes)
        {
            DisplayListID = glGenLists(1);
            glNewList(DisplayListID, GL_COMPILE);
            iVersion = Global::iReCompile; // aktualna wersja siatek (do WireFrame)
        }
    }
    if ((iType == GL_LINES) || (iType == GL_LINE_STRIP) || (iType == GL_LINE_LOOP))
    {
#ifdef USE_VERTEX_ARRAYS
        glVertexPointer(3, GL_DOUBLE, sizeof(vector3), &Points[0].x);
#endif
        glBindTexture(GL_TEXTURE_2D, 0);
#ifdef USE_VERTEX_ARRAYS
        glDrawArrays(iType, 0, iNumPts);
#else
        glBegin(iType);
        for (int i = 0; i < iNumPts; i++)
            glVertex3dv(&Points[i].x);
        glEnd();
#endif
    }
    else if (iType == GL_TRIANGLE_STRIP || iType == GL_TRIANGLE_FAN || iType == GL_TRIANGLES)
    { // jak nie linie, to tr�jk�ty
        TGroundNode *tri = this;
        do
        { // p�tla po obiektach w grupie w celu po��czenia siatek
#ifdef USE_VERTEX_ARRAYS
            glVertexPointer(3, GL_DOUBLE, sizeof(TGroundVertex), &tri->Vertices[0].Point.x);
            glNormalPointer(GL_DOUBLE, sizeof(TGroundVertex), &tri->Vertices[0].Normal.x);
            glTexCoordPointer(2, GL_FLOAT, sizeof(TGroundVertex), &tri->Vertices[0].tu);
#endif
            glColor3ub(tri->Diffuse[0], tri->Diffuse[1], tri->Diffuse[2]);
            glBindTexture(GL_TEXTURE_2D, Global::bWireFrame ? 0 : tri->TextureID);
#ifdef USE_VERTEX_ARRAYS
            glDrawArrays(Global::bWireFrame ? GL_LINE_LOOP : tri->iType, 0, tri->iNumVerts);
#else
            glBegin(Global::bWireFrame ? GL_LINE_LOOP : tri->iType);
            for (int i = 0; i < tri->iNumVerts; i++)
            {
                glNormal3d(tri->Vertices[i].Normal.x, tri->Vertices[i].Normal.y,
                           tri->Vertices[i].Normal.z);
                glTexCoord2f(tri->Vertices[i].tu, tri->Vertices[i].tv);
                glVertex3dv(&tri->Vertices[i].Point.x);
            };
            glEnd();
#endif
            /*
               if (tri->pTriGroup) //je�li z grupy
               {tri=tri->pNext2; //nast�pny w sektorze
                while (tri?!tri->pTriGroup:false) tri=tri->pNext2; //szukamy kolejnego nale��cego do
               grupy
               }
               else
            */
            tri = NULL; // a jak nie, to koniec
        } while (tri);
    }
    else if (iType == TP_MESH)
    { // grupa ze wsp�ln� tekstur� - wrzucanie do wsp�lnego Display List
        if (TextureID)
            glBindTexture(GL_TEXTURE_2D, TextureID); // Ustaw aktywn� tekstur�
        TGroundNode *n = nNode;
        while (n ? n->TextureID == TextureID : false)
        { // wszystkie obiekty o tej samej testurze
            switch (n->iType)
            { // poszczeg�lne typy r�nie si� tworzy
            case TP_TRACK:
            case TP_DUMMYTRACK:
                n->pTrack->Compile(TextureID); // dodanie tr�jk�t�w dla podanej tekstury
                break;
            }
            n = n->nNext3; // nast�pny z listy
        }
    }
    if (!many)
        if (Global::bManageNodes)
            glEndList();
};

void TGroundNode::Release()
{
    if (DisplayListID)
        glDeleteLists(DisplayListID, 1);
    DisplayListID = 0;
};

void TGroundNode::RenderHidden()
{ // renderowanie obiekt�w niewidocznych
    double mgn = SquareMagnitude(pCenter - Global::pCameraPosition);
    switch (iType)
    {
    case TP_SOUND: // McZapkie - dzwiek zapetlony w zaleznosci od odleglosci
        if ((tsStaticSound->GetStatus() & DSBSTATUS_PLAYING) == DSBPLAY_LOOPING)
        {
            tsStaticSound->Play(1, DSBPLAY_LOOPING, true, tsStaticSound->vSoundPosition);
            tsStaticSound->AdjFreq(1.0, Timer::GetDeltaTime());
        }
        return;
    case TP_EVLAUNCH:
        if (EvLaunch->Render())
            if ((EvLaunch->dRadius < 0) || (mgn < EvLaunch->dRadius))
            {
                WriteLog("Eventlauncher " + asName);
                if (Console::Pressed(VK_SHIFT) && (EvLaunch->Event2))
                    Global::AddToQuery(EvLaunch->Event2, NULL);
                else if (EvLaunch->Event1)
                    Global::AddToQuery(EvLaunch->Event1, NULL);
            }
        return;
    }
};

void TGroundNode::RenderDL()
{ // wy�wietlanie obiektu przez Display List
    switch (iType)
    { // obiekty renderowane niezale�nie od odleg�o�ci
    case TP_SUBMODEL:
        TSubModel::fSquareDist = 0;
        return smTerrain->RenderDL();
    }
    // if (pTriGroup) if (pTriGroup!=this) return; //wy�wietla go inny obiekt
    double mgn = SquareMagnitude(pCenter - Global::pCameraPosition);
    if ((mgn > fSquareRadius) || (mgn < fSquareMinRadius)) // McZapkie-070602: nie rysuj odleglych
        // obiektow ale sprawdzaj wyzwalacz
        // zdarzen
        return;
    int i, a;
    switch (iType)
    {
    case TP_TRACK:
        return pTrack->Render();
    case TP_MODEL:
        return Model->RenderDL(&pCenter);
    }
    // TODO: sprawdzic czy jest potrzebny warunek fLineThickness < 0
    // if ((iNumVerts&&(iFlags&0x10))||(iNumPts&&(fLineThickness<0)))
    if ((iFlags & 0x10) || (fLineThickness < 0))
    {
        if (!DisplayListID || (iVersion != Global::iReCompile)) // Ra: wymuszenie rekompilacji
        {
            Compile();
            if (Global::bManageNodes)
                ResourceManager::Register(this);
        };

        if ((iType == GL_LINES) || (iType == GL_LINE_STRIP) || (iType == GL_LINE_LOOP))
        // if (iNumPts)
        { // wszelkie linie s� rysowane na samym ko�cu
            float r, g, b;
            r = Diffuse[0] * Global::ambientDayLight[0]; // w zaleznosci od koloru swiatla
            g = Diffuse[1] * Global::ambientDayLight[1];
            b = Diffuse[2] * Global::ambientDayLight[2];
            glColor4ub(r, g, b, 1.0);
            glCallList(DisplayListID);
            // glColor4fv(Diffuse); //przywr�cenie koloru
            // glColor3ub(Diffuse[0],Diffuse[1],Diffuse[2]);
        }
        // GL_TRIANGLE etc
        else
            glCallList(DisplayListID);
        SetLastUsage(Timer::GetSimulationTime());
    };
};

void TGroundNode::RenderAlphaDL()
{
    // SPOSOB NA POZBYCIE SIE RAMKI DOOKOLA TEXTURY ALPHA DLA OBIEKTOW ZAGNIEZDZONYCH W SCN JAKO
    // NODE

    // W GROUND.H dajemy do klasy TGroundNode zmienna bool PROBLEND to samo robimy w klasie TGround
    // nastepnie podczas wczytywania textury dla TRIANGLES w TGround::AddGroundNode
    // sprawdzamy czy w nazwie jest @ i wg tego
    // ustawiamy PROBLEND na true dla wlasnie wczytywanego trojkata (kazdy trojkat jest osobnym
    // nodem)
    // nastepnie podczas renderowania w bool TGroundNode::RenderAlpha()
    // na poczatku ustawiamy standardowe GL_GREATER = 0.04
    // pozniej sprawdzamy czy jest wlaczony PROBLEND dla aktualnie renderowanego noda TRIANGLE,
    // wlasciwie dla kazdego node'a
    // i jezeli tak to odpowiedni GL_GREATER w przeciwnym wypadku standardowy 0.04

    // if (pTriGroup) if (pTriGroup!=this) return; //wy�wietla go inny obiekt
    double mgn = SquareMagnitude(pCenter - Global::pCameraPosition);
    float r, g, b;
    if (mgn < fSquareMinRadius)
        return;
    if (mgn > fSquareRadius)
        return;
    int i, a;
    switch (iType)
    {
    case TP_TRACTION:
        if (bVisible)
            hvTraction->RenderDL(mgn);
        return;
    case TP_MODEL:
        Model->RenderAlphaDL(&pCenter);
        return;
    case TP_TRACK:
        // pTrack->RenderAlpha();
        return;
    };

    // TODO: sprawdzic czy jest potrzebny warunek fLineThickness < 0
    if ((iNumVerts && (iFlags & 0x20)) || (iNumPts && (fLineThickness > 0)))
    {
#ifdef _PROBLEND
        if ((PROBLEND)) // sprawdza, czy w nazwie nie ma @    //Q: 13122011 - Szociu: 27012012
        {
            glDisable(GL_BLEND);
            glAlphaFunc(GL_GREATER, 0.45); // im mniejsza warto��, tym wi�ksza ramka, domy�lnie 0.1f
        };
#endif
        if (!DisplayListID) //||Global::bReCompile) //Ra: wymuszenie rekompilacji
        {
            Compile();
            if (Global::bManageNodes)
                ResourceManager::Register(this);
        };

        // GL_LINE, GL_LINE_STRIP, GL_LINE_LOOP
        if (iNumPts)
        {
            float linealpha = 255000 * fLineThickness / (mgn + 1.0);
            if (linealpha > 255)
                linealpha = 255;
            r = Diffuse[0] * Global::ambientDayLight[0]; // w zaleznosci od koloru swiatla
            g = Diffuse[1] * Global::ambientDayLight[1];
            b = Diffuse[2] * Global::ambientDayLight[2];
            glColor4ub(r, g, b, linealpha); // przezroczystosc dalekiej linii
            glCallList(DisplayListID);
        }
        // GL_TRIANGLE etc
        else
            glCallList(DisplayListID);
        SetLastUsage(Timer::GetSimulationTime());
    };
#ifdef _PROBLEND
    if ((PROBLEND)) // sprawdza, czy w nazwie nie ma @    //Q: 13122011 - Szociu: 27012012
    {
        glEnable(GL_BLEND);
        glAlphaFunc(GL_GREATER, 0.04);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    };
#endif
}

//------------------------------------------------------------------------------
//------------------ Podstawowy pojemnik terenu - sektor -----------------------
//------------------------------------------------------------------------------
TSubRect::TSubRect()
{
    nRootNode = NULL; // lista wszystkich obiekt�w jest pusta
    nRenderHidden = nRenderRect = nRenderRectAlpha = nRender = nRenderMixed = nRenderAlpha =
        nRenderWires = NULL;
    tTrackAnim = NULL; // nic nie animujemy
    tTracks = NULL; // nie ma jeszcze tor�w
    nRootMesh = nMeshed = NULL; // te listy te� s� puste
    iNodeCount = 0; // licznik obiekt�w
    iTracks = 0; // licznik tor�w
}
TSubRect::~TSubRect()
{
    if (Global::bManageNodes) // Ra: tu si� co� sypie
        ResourceManager::Unregister(this); // wyrejestrowanie ze sprz�tacza
    // TODO: usun�� obiekty z listy (nRootMesh), bo s� one tworzone dla sektora
}

void TSubRect::NodeAdd(TGroundNode *Node)
{ // przyczepienie obiektu do sektora, wst�pna kwalifikacja na listy renderowania
    if (!this)
        return; // zabezpiecznie przed obiektami przekraczaj�cymi obszar roboczy
    // Ra: sortowanie obiekt�w na listy renderowania:
    // nRenderHidden    - lista obiekt�w niewidocznych, "renderowanych" r�wnie� z ty�u
    // nRenderRect      - lista grup renderowanych z sektora
    // nRenderRectAlpha - lista grup renderowanych z sektora z przezroczysto�ci�
    // nRender          - lista grup renderowanych z w�asnych VBO albo DL
    // nRenderAlpha     - lista grup renderowanych z w�asnych VBO albo DL z przezroczysto�ci�
    // nRenderWires     - lista grup renderowanych z w�asnych VBO albo DL - druty i linie
    // nMeshed          - obiekty do pogrupowania wg tekstur
    GLuint t; // pomocniczy kod tekstury
    switch (Node->iType)
    {
    case TP_SOUND: // te obiekty s� sprawdzanie niezale�nie od kierunku patrzenia
    case TP_EVLAUNCH:
        Node->nNext3 = nRenderHidden;
        nRenderHidden = Node; // do listy koniecznych
        break;
    case TP_TRACK: // TODO: tory z cieniem (tunel, canyon) te� da� bez ��czenia?
        ++iTracks; // jeden tor wi�cej
        Node->pTrack->RaOwnerSet(this); // do kt�rego sektora ma zg�asza� animacj�
        // if (Global::bUseVBO?false:!Node->pTrack->IsGroupable())
        if (Global::bUseVBO ? true :
                              !Node->pTrack->IsGroupable()) // TODO: tymczasowo dla VBO wy��czone
            RaNodeAdd(
                Node); // tory ruchome nie s� grupowane przy Display Lists (wymagaj� od�wie�ania DL)
        else
        { // tory nieruchome mog� by� pogrupowane wg tekstury, przy VBO wszystkie
            Node->TextureID = Node->pTrack->TextureGet(0); // pobranie tekstury do sortowania
            t = Node->pTrack->TextureGet(1);
            if (Node->TextureID) // je�eli jest pierwsza
            {
                if (t && (Node->TextureID != t))
                { // je�li s� dwie r�ne tekstury, dodajemy drugi obiekt dla danego toru
                    TGroundNode *n = new TGroundNode();
                    n->iType = TP_DUMMYTRACK; // obiekt renderuj�cy siatki dla tekstury
                    n->TextureID = t;
                    n->pTrack = Node->pTrack; // wskazuje na ten sam tor
                    n->pCenter = Node->pCenter;
                    n->fSquareRadius = Node->fSquareRadius;
                    n->fSquareMinRadius = Node->fSquareMinRadius;
                    n->iFlags = Node->iFlags;
                    n->nNext2 = nRootMesh;
                    nRootMesh = n; // podczepienie do listy, �eby usun�� na ko�cu
                    n->nNext3 = nMeshed;
                    nMeshed = n;
                }
            }
            else
                Node->TextureID = t; // jest tylko druga tekstura
            if (Node->TextureID)
            {
                Node->nNext3 = nMeshed;
                nMeshed = Node;
            } // do podzielenia potem
        }
        break;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_TRIANGLES:
        // Node->nNext3=nMeshed; nMeshed=Node; //do podzielenia potem
        if (Node->iFlags & 0x20) // czy jest przezroczyste?
        {
            Node->nNext3 = nRenderRectAlpha;
            nRenderRectAlpha = Node;
        } // DL: do przezroczystych z sektora
        else if (Global::bUseVBO)
        {
            Node->nNext3 = nRenderRect;
            nRenderRect = Node;
        } // VBO: do nieprzezroczystych z sektora
        else
        {
            Node->nNext3 = nRender;
            nRender = Node;
        } // DL: do nieprzezroczystych wszelakich
        /*
           //Ra: na razie wy��czone do test�w VBO
           //if
           ((Node->iType==GL_TRIANGLE_STRIP)||(Node->iType==GL_TRIANGLE_FAN)||(Node->iType==GL_TRIANGLES))
            if (Node->fSquareMinRadius==0.0) //znikaj�ce z bliska nie mog� by� optymalizowane
             if (Node->fSquareRadius>=160000.0) //tak od 400m to ju� normalne tr�jk�ty musz� by�
             //if (Node->iFlags&0x10) //i nieprzezroczysty
             {if (pTriGroup) //je�eli by� ju� jaki� grupuj�cy
              {if (pTriGroup->fSquareRadius>Node->fSquareRadius) //i mia� wi�kszy zasi�g
                Node->fSquareRadius=pTriGroup->fSquareRadius; //zwi�kszenie zakresu widoczno�ci
           grupuj�cego
               pTriGroup->pTriGroup=Node; //poprzedniemu doczepiamy nowy
              }
              Node->pTriGroup=Node; //nowy lider ma si� sam wy�wietla� - wska�nik na siebie
              pTriGroup=Node; //zapami�tanie lidera
             }
        */
        break;
    case TP_TRACTION:
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP: // te renderowane na ko�cu, �eby nie �apa�y koloru nieba
        Node->nNext3 = nRenderWires;
        nRenderWires = Node; // lista drut�w
        break;
    case TP_MODEL: // modele zawsze wy�wietlane z w�asnego VBO
        // je�li model jest prosty, mo�na pr�bowa� zrobi� wsp�ln� siatk� (s�upy)
        if ((Node->iFlags & 0x20200020) == 0) // czy brak przezroczysto�ci?
        {
            Node->nNext3 = nRender;
            nRender = Node;
        } // do nieprzezroczystych
        else if ((Node->iFlags & 0x10100010) == 0) // czy brak nieprzezroczysto�ci?
        {
            Node->nNext3 = nRenderAlpha;
            nRenderAlpha = Node;
        } // do przezroczystych
        else // jak i take i takie, to b�dzie dwa razy renderowane...
        {
            Node->nNext3 = nRenderMixed;
            nRenderMixed = Node;
        } // do mieszanych
        // Node->nNext3=nMeshed; //dopisanie do listy sortowania
        // nMeshed=Node;
        break;
    case TP_MEMCELL:
    case TP_TRACTIONPOWERSOURCE: // a te w og�le pomijamy
        //  case TP_ISOLATED: //lista tor�w w obwodzie izolowanym - na razie ignorowana
        break;
    case TP_DYNAMIC:
        return; // tych nie dopisujemy wcale
    }
    Node->nNext2 = nRootNode; // dopisanie do og�lnej listy
    nRootNode = Node;
    ++iNodeCount; // licznik obiekt�w
}

void TSubRect::RaNodeAdd(TGroundNode *Node)
{ // finalna kwalifikacja na listy renderowania, je�li nie obs�ugiwane grupowo
    switch (Node->iType)
    {
    case TP_TRACK:
        if (Global::bUseVBO)
        {
            Node->nNext3 = nRenderRect;
            nRenderRect = Node;
        } // VBO: do nieprzezroczystych z sektora
        else
        {
            Node->nNext3 = nRender;
            nRender = Node;
        } // DL: do nieprzezroczystych
        break;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_TRIANGLES:
        if (Node->iFlags & 0x20) // czy jest przezroczyste?
        {
            Node->nNext3 = nRenderRectAlpha;
            nRenderRectAlpha = Node;
        } // DL: do przezroczystych z sektora
        else if (Global::bUseVBO)
        {
            Node->nNext3 = nRenderRect;
            nRenderRect = Node;
        } // VBO: do nieprzezroczystych z sektora
        else
        {
            Node->nNext3 = nRender;
            nRender = Node;
        } // DL: do nieprzezroczystych wszelakich
        break;
    case TP_MODEL: // modele zawsze wy�wietlane z w�asnego VBO
        if ((Node->iFlags & 0x20200020) == 0) // czy brak przezroczysto�ci?
        {
            Node->nNext3 = nRender;
            nRender = Node;
        } // do nieprzezroczystych
        else if ((Node->iFlags & 0x10100010) == 0) // czy brak nieprzezroczysto�ci?
        {
            Node->nNext3 = nRenderAlpha;
            nRenderAlpha = Node;
        } // do przezroczystych
        else // jak i take i takie, to b�dzie dwa razy renderowane...
        {
            Node->nNext3 = nRenderMixed;
            nRenderMixed = Node;
        } // do mieszanych
        break;
    case TP_MESH: // grupa ze wsp�ln� tekstur�
        //{Node->nNext3=nRenderRect; nRenderRect=Node;} //do nieprzezroczystych z sektora
        {
            Node->nNext3 = nRender;
            nRender = Node;
        } // do nieprzezroczystych
        break;
    case TP_SUBMODEL: // submodele terenu w kwadracie kilometrowym id� do nRootMesh
        // WriteLog("nRootMesh was "+AnsiString(nRootMesh?"not null ":"null
        // ")+IntToHex(int(this),8));
        Node->nNext3 = nRootMesh; // przy VBO musi by� inaczej
        nRootMesh = Node;
        break;
    }
}

void TSubRect::Sort()
{ // przygotowanie sektora do renderowania
    TGroundNode **n0, *n1, *n2; // wska�niki robocze
    delete[] tTracks; // usuni�cie listy
    tTracks =
        iTracks ? new TTrack *[iTracks] : NULL; // tworzenie tabeli tor�w do renderowania pojazd�w
    if (tTracks)
    { // wype�nianie tabeli tor�w
        int i = 0;
        for (n1 = nRootNode; n1; n1 = n1->nNext2) // kolejne obiekty z sektora
            if (n1->iType == TP_TRACK)
                tTracks[i++] = n1->pTrack;
    }
    // sortowanie obiekt�w w sektorze na listy renderowania
    if (!nMeshed)
        return; // nie ma nic do sortowania
    bool sorted = false;
    while (!sorted)
    { // sortowanie b�belkowe obiekt�w wg tekstury
        sorted = true; // zak�adamy posortowanie
        n0 = &nMeshed; // wska�nik niezb�dny do zamieniania obiekt�w
        n1 = nMeshed; // lista obiekt�w przetwarzanych na statyczne siatki
        while (n1)
        { // sprawdzanie stanu posortowania obiekt�w i ewentualne zamiany
            n2 = n1->nNext3; // kolejny z tej listy
            if (n2) // je�li istnieje
                if (n1->TextureID > n2->TextureID)
                { // zamiana element�w miejscami
                    *n0 = n2; // drugi b�dzie na pocz�tku
                    n1->nNext3 = n2->nNext3; // ten zza drugiego b�dzie za pierwszym
                    n2->nNext3 = n1; // a za drugim b�dzie pierwszy
                    sorted = false; // potrzebny kolejny przebieg
                }
            n0 = &(n1->nNext3);
            n1 = n2;
        };
    }
    // wyrzucenie z listy obiekt�w pojedynczych (nie ma z czym ich grupowa�)
    // nawet jak s� pojedyncze, to i tak lepiej, aby by�y w jednym Display List
    /*
        else
        {//dodanie do zwyk�ej listy renderowania i usuni�cie z grupowego
         *n0=n2; //drugi b�dzie na pocz�tku
         RaNodeAdd(n1); //nie ma go z czym zgrupowa�; (n1->nNext3) zostanie nadpisane
         n1=n2; //potrzebne do ustawienia (n0)
        }
    */
    //...
    // przegl�danie listy i tworzenie obiekt�w renderuj�cych dla danej tekstury
    GLuint t = 0; // pomocniczy kod tekstury
    n1 = nMeshed; // lista obiekt�w przetwarzanych na statyczne siatki
    while (n1)
    { // dla ka�dej tekstury powinny istnie� co najmniej dwa obiekty, ale dla DL nie ma to znaczenia
        if (t < n1->TextureID) // je�li (n1) ma inn� tekstur� ni� poprzednie
        { // mo�na zrobi� obiekt renderuj�cy
            t = n1->TextureID;
            n2 = new TGroundNode();
            n2->nNext2 = nRootMesh;
            nRootMesh = n2; // podczepienie na pocz�tku listy
            nRootMesh->iType = TP_MESH; // obiekt renderuj�cy siatki dla tekstury
            nRootMesh->TextureID = t;
            nRootMesh->nNode = n1; // pierwszy element z listy
            nRootMesh->pCenter = n1->pCenter;
            nRootMesh->fSquareRadius = 1e8; // wida� bez ogranicze�
            nRootMesh->fSquareMinRadius = 0.0;
            nRootMesh->iFlags = 0x10;
            RaNodeAdd(nRootMesh); // dodanie do odpowiedniej listy renderowania
        }
        n1 = n1->nNext3; // kolejny z tej listy
    };
}

TTrack * TSubRect::FindTrack(vector3 *Point, int &iConnection, TTrack *Exclude)
{ // szukanie toru, kt�rego koniec jest najbli�szy (*Point)
    TTrack *Track;
    for (int i = 0; i < iTracks; ++i)
        if (tTracks[i] != Exclude) // mo�na u�y� tabel� tor�w, bo jest mniejsza
        {
            iConnection = tTracks[i]->TestPoint(Point);
            if (iConnection >= 0)
                return tTracks[i]; // szukanie TGroundNode nie jest potrzebne
        }
    /*
     TGroundNode *Current;
     for (Current=nRootNode;Current;Current=Current->Next)
      if ((Current->iType==TP_TRACK)&&(Current->pTrack!=Exclude)) //mo�na u�y� tabel� tor�w
       {
        iConnection=Current->pTrack->TestPoint(Point);
        if (iConnection>=0) return Current;
       }
    */
    return NULL;
};

bool TSubRect::RaTrackAnimAdd(TTrack *t)
{ // aktywacja animacji tor�w w VBO (zwrotnica, obrotnica)
    if (m_nVertexCount < 0)
        return true; // nie ma animacji, gdy nie wida�
    if (tTrackAnim)
        tTrackAnim->RaAnimListAdd(t);
    else
        tTrackAnim = t;
    return false; // b�dzie animowane...
}

void TSubRect::RaAnimate()
{ // wykonanie animacji
    if (!tTrackAnim)
        return; // nie ma nic do animowania
    if (Global::bUseVBO)
    { // od�wie�enie VBO sektora
        if (Global::bOpenGL_1_5) // modyfikacje VBO s� dost�pne od OpenGL 1.5
            glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_nVBOVertices);
        else // dla OpenGL 1.4 z GL_ARB_vertex_buffer_object od�wie�enie ca�ego sektora
            Release(); // opr�nienie VBO sektora, aby si� od�wie�y� z nowymi ustawieniami
    }
    tTrackAnim = tTrackAnim->RaAnimate(); // przeliczenie animacji kolejnego
};

TTraction * TSubRect::FindTraction(vector3 *Point, int &iConnection, TTraction *Exclude)
{ // szukanie prz�s�a w sektorze, kt�rego koniec jest najbli�szy (*Point)
    TGroundNode *Current;
    for (Current = nRenderWires; Current; Current = Current->nNext3)
        if ((Current->iType == TP_TRACTION) && (Current->hvTraction != Exclude))
        {
            iConnection = Current->hvTraction->TestPoint(Point);
            if (iConnection >= 0)
                return Current->hvTraction;
        }
    return NULL;
};

void TSubRect::LoadNodes()
{ // utworzenie siatek VBO dla wszystkich node w sektorze
    if (m_nVertexCount >= 0)
        return; // obiekty by�y ju� sprawdzone
    m_nVertexCount = 0; //-1 oznacza, �e nie sprawdzono listy obiekt�w
    if (!nRootNode)
        return;
    TGroundNode *n = nRootNode;
    while (n)
    {
        switch (n->iType)
        {
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
        case GL_TRIANGLES:
            n->iVboPtr = m_nVertexCount; // nowy pocz�tek
            m_nVertexCount += n->iNumVerts;
            break;
        case GL_LINES:
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            n->iVboPtr = m_nVertexCount; // nowy pocz�tek
            m_nVertexCount +=
                n->iNumPts; // miejsce w tablicach normalnych i teksturowania si� zmarnuje...
            break;
        case TP_TRACK:
            n->iVboPtr = m_nVertexCount; // nowy pocz�tek
            n->iNumVerts = n->pTrack->RaArrayPrepare(); // zliczenie wierzcho�k�w
            m_nVertexCount += n->iNumVerts;
            break;
        case TP_TRACTION:
            n->iVboPtr = m_nVertexCount; // nowy pocz�tek
            n->iNumVerts = n->hvTraction->RaArrayPrepare(); // zliczenie wierzcho�k�w
            m_nVertexCount += n->iNumVerts;
            break;
        }
        n = n->nNext2; // nast�pny z sektora
    }
    if (!m_nVertexCount)
        return; // je�li nie ma obiekt�w do wy�wietlenia z VBO, to koniec
    if (Global::bUseVBO)
    { // tylko liczenie wierzcho��w, gdy nie ma VBO
        MakeArray(m_nVertexCount);
        n = nRootNode;
        int i;
        while (n)
        {
            if (n->iVboPtr >= 0)
                switch (n->iType)
                {
                case GL_TRIANGLE_STRIP:
                case GL_TRIANGLE_FAN:
                case GL_TRIANGLES:
                    for (i = 0; i < n->iNumVerts; ++i)
                    { // Ra: tr�jk�ty mo�na od razu wczytywa� do takich tablic... to mo�e poczeka�
                        m_pVNT[n->iVboPtr + i].x = n->Vertices[i].Point.x;
                        m_pVNT[n->iVboPtr + i].y = n->Vertices[i].Point.y;
                        m_pVNT[n->iVboPtr + i].z = n->Vertices[i].Point.z;
                        m_pVNT[n->iVboPtr + i].nx = n->Vertices[i].Normal.x;
                        m_pVNT[n->iVboPtr + i].ny = n->Vertices[i].Normal.y;
                        m_pVNT[n->iVboPtr + i].nz = n->Vertices[i].Normal.z;
                        m_pVNT[n->iVboPtr + i].u = n->Vertices[i].tu;
                        m_pVNT[n->iVboPtr + i].v = n->Vertices[i].tv;
                    }
                    break;
                case GL_LINES:
                case GL_LINE_STRIP:
                case GL_LINE_LOOP:
                    for (i = 0; i < n->iNumPts; ++i)
                    {
                        m_pVNT[n->iVboPtr + i].x = n->Points[i].x;
                        m_pVNT[n->iVboPtr + i].y = n->Points[i].y;
                        m_pVNT[n->iVboPtr + i].z = n->Points[i].z;
                        // miejsce w tablicach normalnych i teksturowania si� marnuje...
                    }
                    break;
                case TP_TRACK:
                    if (n->iNumVerts) // bo tory zabezpieczaj�ce s� niewidoczne
                        n->pTrack->RaArrayFill(m_pVNT + n->iVboPtr, m_pVNT);
                    break;
                case TP_TRACTION:
                    if (n->iNumVerts) // druty mog� by� niewidoczne...?
                        n->hvTraction->RaArrayFill(m_pVNT + n->iVboPtr);
                    break;
                }
            n = n->nNext2; // nast�pny z sektora
        }
        BuildVBOs();
    }
    if (Global::bManageNodes)
        ResourceManager::Register(this); // dodanie do automatu zwalniaj�cego pami��
}

bool TSubRect::StartVBO()
{ // pocz�tek rysowania element�w z VBO w sektorze
    SetLastUsage(Timer::GetSimulationTime()); // te z ty�u b�d� niepotrzebnie zwalniane
    return CMesh::StartVBO();
};

void TSubRect::Release()
{ // wirtualne zwolnienie zasob�w przez sprz�tacz albo destruktor
    if (Global::bUseVBO)
        CMesh::Clear(); // usuwanie bufor�w
};

void TSubRect::RenderDL()
{ // renderowanie nieprzezroczystych (DL)
    TGroundNode *node;
    RaAnimate(); // przeliczenia animacji tor�w w sektorze
    for (node = nRender; node; node = node->nNext3)
        node->RenderDL(); // nieprzezroczyste obiekty (opr�cz pojazd�w)
    for (node = nRenderMixed; node; node = node->nNext3)
        node->RenderDL(); // nieprzezroczyste z mieszanych modeli
    for (int j = 0; j < iTracks; ++j)
        tTracks[j]->RenderDyn(); // nieprzezroczyste fragmenty pojazd�w na torach
};

void TSubRect::RenderAlphaDL()
{ // renderowanie przezroczystych modeli oraz pojazd�w (DL)
    TGroundNode *node;
    for (node = nRenderMixed; node; node = node->nNext3)
        node->RenderAlphaDL(); // przezroczyste z mieszanych modeli
    for (node = nRenderAlpha; node; node = node->nNext3)
        node->RenderAlphaDL(); // przezroczyste modele
    // for (node=tmp->nRender;node;node=node->nNext3)
    // if (node->iType==TP_TRACK)
    //  node->pTrack->RenderAlpha(); //przezroczyste fragmenty pojazd�w na torach
    for (int j = 0; j < iTracks; ++j)
        tTracks[j]->RenderDynAlpha(); // przezroczyste fragmenty pojazd�w na torach
};

void TSubRect::RenderVBO()
{ // renderowanie nieprzezroczystych (VBO)
    TGroundNode *node;
    RaAnimate(); // przeliczenia animacji tor�w w sektorze
    LoadNodes(); // czemu tutaj?
    if (StartVBO())
    {
        for (node = nRenderRect; node; node = node->nNext3)
            if (node->iVboPtr >= 0)
                node->RenderVBO(); // nieprzezroczyste obiekty terenu
        EndVBO();
    }
    for (node = nRender; node; node = node->nNext3)
        node->RenderVBO(); // nieprzezroczyste obiekty (opr�cz pojazd�w)
    for (node = nRenderMixed; node; node = node->nNext3)
        node->RenderVBO(); // nieprzezroczyste z mieszanych modeli
    for (int j = 0; j < iTracks; ++j)
        tTracks[j]->RenderDyn(); // nieprzezroczyste fragmenty pojazd�w na torach
};

void TSubRect::RenderAlphaVBO()
{ // renderowanie przezroczystych modeli oraz pojazd�w (VBO)
    TGroundNode *node;
    for (node = nRenderMixed; node; node = node->nNext3)
        node->RenderAlphaVBO(); // przezroczyste z mieszanych modeli
    for (node = nRenderAlpha; node; node = node->nNext3)
        node->RenderAlphaVBO(); // przezroczyste modele
    // for (node=tmp->nRender;node;node=node->nNext3)
    // if (node->iType==TP_TRACK)
    //  node->pTrack->RenderAlpha(); //przezroczyste fragmenty pojazd�w na torach
    for (int j = 0; j < iTracks; ++j)
        tTracks[j]->RenderDynAlpha(); // przezroczyste fragmenty pojazd�w na torach
};

void TSubRect::RenderSounds()
{ // aktualizacja d�wi�k�w w pojazdach sektora (sektor mo�e nie by� wy�wietlany)
    for (int j = 0; j < iTracks; ++j)
        tTracks[j]->RenderDynSounds(); // d�wi�ki pojazd�w id� niezale�nie od wy�wietlania
};
//---------------------------------------------------------------------------
//------------------ Kwadrat kilometrowy ------------------------------------
//---------------------------------------------------------------------------
int TGroundRect::iFrameNumber = 0; // licznik wy�wietlanych klatek

TGroundRect::TGroundRect()
{
    pSubRects = NULL;
    nTerrain = NULL;
};

TGroundRect::~TGroundRect()
{
    SafeDeleteArray(pSubRects);
};

void TGroundRect::RenderDL()
{ // renderowanie kwadratu kilometrowego (DL), je�li jeszcze nie zrobione
    if (iLastDisplay != iFrameNumber)
    { // tylko jezeli dany kwadrat nie by� jeszcze renderowany
        // for (TGroundNode* node=pRender;node;node=node->pNext3)
        // node->Render(); //nieprzezroczyste tr�jk�ty kwadratu kilometrowego
        if (nRender)
        { //��czenie tr�jk�t�w w jedn� list� - troch� wioska
            if (!nRender->DisplayListID || (nRender->iVersion != Global::iReCompile))
            { // je�eli nie skompilowany, kompilujemy wszystkie tr�jk�ty w jeden
                nRender->fSquareRadius = 5000.0 * 5000.0; // aby agregat nigdy nie znika�
                nRender->DisplayListID = glGenLists(1);
                glNewList(nRender->DisplayListID, GL_COMPILE);
                nRender->iVersion = Global::iReCompile; // aktualna wersja siatek
                for (TGroundNode *node = nRender; node; node = node->nNext3) // nast�pny tej grupy
                    node->Compile(true);
                glEndList();
            }
            nRender->RenderDL(); // nieprzezroczyste tr�jk�ty kwadratu kilometrowego
        }
        if (nRootMesh)
            nRootMesh->RenderDL();
        iLastDisplay = iFrameNumber; // drugi raz nie potrzeba
    }
};

void TGroundRect::RenderVBO()
{ // renderowanie kwadratu kilometrowego (VBO), je�li jeszcze nie zrobione
    if (iLastDisplay != iFrameNumber)
    { // tylko jezeli dany kwadrat nie by� jeszcze renderowany
        LoadNodes(); // ewentualne tworzenie siatek
        if (StartVBO())
        {
            for (TGroundNode *node = nRenderRect; node; node = node->nNext3) // nast�pny tej grupy
                node->RaRenderVBO(); // nieprzezroczyste tr�jk�ty kwadratu kilometrowego
            EndVBO();
            iLastDisplay = iFrameNumber;
        }
        if (nTerrain)
            nTerrain->smTerrain->iVisible = iFrameNumber; // ma si� wy�wietli� w tej ramce
    }
};

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

void TGround::MoveGroundNode(vector3 pPosition)
{ // Ra: to wymaga gruntownej reformy
    /*
     TGroundNode *Current;
     for (Current=RootNode;Current!=NULL;Current=Current->Next)
      Current->MoveMe(pPosition);

     TGroundRect *Rectx=new TGroundRect; //kwadrat kilometrowy
     for(int i=0;i<iNumRects;i++)
      for(int j=0;j<iNumRects;j++)
       Rects[i][j]=*Rectx; //kopiowanie zawarto�ci do ka�dego kwadratu
     delete Rectx;
     for (Current=RootNode;Current!=NULL;Current=Current->Next)
     {//roz�o�enie obiekt�w na mapie
      if (Current->iType!=TP_DYNAMIC)
      {//pojazd�w to w og�le nie dotyczy
       if ((Current->iType!=GL_TRIANGLES)&&(Current->iType!=GL_TRIANGLE_STRIP)?true //~czy tr�jk�t?
        :(Current->iFlags&0x20)?true //~czy tekstur� ma nieprzezroczyst�?
         //:(Current->iNumVerts!=3)?true //~czy tylko jeden tr�jk�t?
         :(Current->fSquareMinRadius!=0.0)?true //~czy widoczny z bliska?
          :(Current->fSquareRadius<=90000.0)) //~czy widoczny z daleka?
        GetSubRect(Current->pCenter.x,Current->pCenter.z)->AddNode(Current);
       else //dodajemy do kwadratu kilometrowego
        GetRect(Current->pCenter.x,Current->pCenter.z)->AddNode(Current);
      }
     }
     for (Current=RootDynamic;Current!=NULL;Current=Current->Next)
     {
      Current->pCenter+=pPosition;
      Current->DynamicObject->UpdatePos();
     }
     for (Current=RootDynamic;Current!=NULL;Current=Current->Next)
      Current->DynamicObject->MoverParameters->Physic_ReActivation();
    */
}

TGround::TGround()
{
    // RootNode=NULL;
    nRootDynamic = NULL;
    QueryRootEvent = NULL;
    tmpEvent = NULL;
    tmp2Event = NULL;
    OldQRE = NULL;
    RootEvent = NULL;
    iNumNodes = 0;
    // pTrain=NULL;
    Global::pGround = this;
    bInitDone = false; // Ra: �eby nie robi�o dwa razy FirstInit
    for (int i = 0; i < TP_LAST; i++)
        nRootOfType[i] = NULL; // zerowanie tablic wyszukiwania
    bDynamicRemove = false; // na razie nic do usuni�cia
    sTracks = new TNames(); // nazwy tor�w - na razie tak
}

TGround::~TGround()
{
    Free();
}

void TGround::Free()
{
    TEvent *tmp;
    for (TEvent *Current = RootEvent; Current;)
    {
        tmp = Current;
        Current = Current->evNext2;
        delete tmp;
    }
    TGroundNode *tmpn;
    for (int i = 0; i < TP_LAST; ++i)
    {
        for (TGroundNode *Current = nRootOfType[i]; Current;)
        {
            tmpn = Current;
            Current = Current->nNext;
            delete tmpn;
        }
        nRootOfType[i] = NULL;
    }
    for (TGroundNode *Current = nRootDynamic; Current;)
    {
        tmpn = Current;
        Current = Current->nNext;
        delete tmpn;
    }
    iNumNodes = 0;
    // RootNode=NULL;
    nRootDynamic = NULL;
    delete sTracks;
}

TGroundNode * TGround::DynamicFindAny(AnsiString asNameToFind)
{ // wyszukanie pojazdu o podanej nazwie, szukanie po wszystkich (u�y� drzewa!)
    for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
        if ((Current->asName == asNameToFind))
            return Current;
    return NULL;
};

TGroundNode * TGround::DynamicFind(AnsiString asNameToFind)
{ // wyszukanie pojazdu z obsad� o podanej nazwie (u�y� drzewa!)
    for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
        if (Current->DynamicObject->Mechanik)
            if ((Current->asName == asNameToFind))
                return Current;
    return NULL;
};

void TGround::DynamicList(bool all)
{ // odes�anie nazw pojazd�w dost�pnych na scenerii (nazwy, szczeg�lnie wagon�w, mog� si�
    // powtarza�!)
    for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
        if (all || Current->DynamicObject->Mechanik)
            WyslijString(Current->asName, 6); // same nazwy pojazd�w
    WyslijString("none", 6); // informacja o ko�cu listy
};

TGroundNode * TGround::FindGroundNode(AnsiString asNameToFind, TGroundNodeType iNodeType)
{ // wyszukiwanie obiektu o podanej nazwie i konkretnym typie
    if ((iNodeType == TP_TRACK) || (iNodeType == TP_MEMCELL) || (iNodeType == TP_MODEL))
    { // wyszukiwanie w drzewie binarnym
        return (TGroundNode *)sTracks->Find(iNodeType, asNameToFind.c_str());
    }
    // standardowe wyszukiwanie liniowe
    TGroundNode *Current;
    for (Current = nRootOfType[iNodeType]; Current; Current = Current->nNext)
        if (Current->asName == asNameToFind)
            return Current;
    return NULL;
}

double fTrainSetVel = 0;
double fTrainSetDir = 0;
double fTrainSetDist = 0; // odleg�o�� sk�adu od punktu 1 w stron� punktu 2
AnsiString asTrainSetTrack = "";
int iTrainSetConnection = 0;
bool bTrainSet = false;
AnsiString asTrainName = "";
int iTrainSetWehicleNumber = 0;
TGroundNode *nTrainSetNode = NULL; // poprzedni pojazd do ��czenia
TGroundNode *nTrainSetDriver = NULL; // pojazd, kt�remu zostanie wys�any rozk�ad

TGroundVertex TempVerts[10000]; // tu wczytywane s� tr�jk�ty
Byte TempConnectionType[200]; // Ra: sprz�gi w sk�adzie; ujemne, gdy odwrotnie

void TGround::RaTriangleDivider(TGroundNode *node)
{ // tworzy dodatkowe tr�jk�ty i zmiejsza podany
    // to jest wywo�ywane przy wczytywaniu tr�jk�t�w
    // dodatkowe tr�jk�ty s� dodawane do g��wnej listy tr�jk�t�w
    // podzia� tr�jk�t�w na sektory i kwadraty jest dokonywany p�niej w FirstInit
    if (node->iType != GL_TRIANGLES)
        return; // tylko pojedyncze tr�jk�ty
    if (node->iNumVerts != 3)
        return; // tylko gdy jeden tr�jk�t
    double x0 = 1000.0 * floor(0.001 * node->pCenter.x) - 200.0;
    double x1 = x0 + 1400.0;
    double z0 = 1000.0 * floor(0.001 * node->pCenter.z) - 200.0;
    double z1 = z0 + 1400.0;
    if ((node->Vertices[0].Point.x >= x0) && (node->Vertices[0].Point.x <= x1) &&
        (node->Vertices[0].Point.z >= z0) && (node->Vertices[0].Point.z <= z1) &&
        (node->Vertices[1].Point.x >= x0) && (node->Vertices[1].Point.x <= x1) &&
        (node->Vertices[1].Point.z >= z0) && (node->Vertices[1].Point.z <= z1) &&
        (node->Vertices[2].Point.x >= x0) && (node->Vertices[2].Point.x <= x1) &&
        (node->Vertices[2].Point.z >= z0) && (node->Vertices[2].Point.z <= z1))
        return; // tr�jk�t wystaj�cy mniej ni� 200m z kw. kilometrowego jest do przyj�cia
    // Ra: przerobi� na dzielenie na 2 tr�jk�ty, podzia� w przeci�ciu z siatk� kilometrow�
    // Ra: i z rekurencj� b�dzie dzieli� trzy tr�jk�ty, je�li b�dzie taka potrzeba
    int divide = -1; // bok do podzielenia: 0=AB, 1=BC, 2=CA; +4=podzia� po OZ; +8 na x1/z1
    double min = 0, mul; // je�li przechodzi przez o�, iloczyn b�dzie ujemny
    x0 += 200.0;
    x1 -= 200.0; // przestawienie na siatk�
    z0 += 200.0;
    z1 -= 200.0;
    mul = (node->Vertices[0].Point.x - x0) * (node->Vertices[1].Point.x - x0); // AB na wschodzie
    if (mul < min)
        min = mul, divide = 0;
    mul = (node->Vertices[1].Point.x - x0) * (node->Vertices[2].Point.x - x0); // BC na wschodzie
    if (mul < min)
        min = mul, divide = 1;
    mul = (node->Vertices[2].Point.x - x0) * (node->Vertices[0].Point.x - x0); // CA na wschodzie
    if (mul < min)
        min = mul, divide = 2;
    mul = (node->Vertices[0].Point.x - x1) * (node->Vertices[1].Point.x - x1); // AB na zachodzie
    if (mul < min)
        min = mul, divide = 8;
    mul = (node->Vertices[1].Point.x - x1) * (node->Vertices[2].Point.x - x1); // BC na zachodzie
    if (mul < min)
        min = mul, divide = 9;
    mul = (node->Vertices[2].Point.x - x1) * (node->Vertices[0].Point.x - x1); // CA na zachodzie
    if (mul < min)
        min = mul, divide = 10;
    mul = (node->Vertices[0].Point.z - z0) * (node->Vertices[1].Point.z - z0); // AB na po�udniu
    if (mul < min)
        min = mul, divide = 4;
    mul = (node->Vertices[1].Point.z - z0) * (node->Vertices[2].Point.z - z0); // BC na po�udniu
    if (mul < min)
        min = mul, divide = 5;
    mul = (node->Vertices[2].Point.z - z0) * (node->Vertices[0].Point.z - z0); // CA na po�udniu
    if (mul < min)
        min = mul, divide = 6;
    mul = (node->Vertices[0].Point.z - z1) * (node->Vertices[1].Point.z - z1); // AB na p�nocy
    if (mul < min)
        min = mul, divide = 12;
    mul = (node->Vertices[1].Point.z - z1) * (node->Vertices[2].Point.z - z1); // BC na p�nocy
    if (mul < min)
        min = mul, divide = 13;
    mul = (node->Vertices[2].Point.z - z1) * (node->Vertices[0].Point.z - z1); // CA na p�nocy
    if (mul < min)
        divide = 14;
    // tworzymy jeden dodatkowy tr�jk�t, dziel�c jeden bok na przeci�ciu siatki kilometrowej
    TGroundNode *ntri; // wska�nik na nowy tr�jk�t
    ntri = new TGroundNode(); // a ten jest nowy
    ntri->iType = GL_TRIANGLES; // kopiowanie parametr�w, przyda�by si� konstruktor kopiuj�cy
    ntri->Init(3);
    ntri->TextureID = node->TextureID;
    ntri->iFlags = node->iFlags;
    for (int j = 0; j < 4; ++j)
    {
        ntri->Ambient[j] = node->Ambient[j];
        ntri->Diffuse[j] = node->Diffuse[j];
        ntri->Specular[j] = node->Specular[j];
    }
    ntri->asName = node->asName;
    ntri->fSquareRadius = node->fSquareRadius;
    ntri->fSquareMinRadius = node->fSquareMinRadius;
    ntri->bVisible = node->bVisible; // a s� jakie� niewidoczne?
    ntri->nNext = nRootOfType[GL_TRIANGLES];
    nRootOfType[GL_TRIANGLES] = ntri; // dopisanie z przodu do listy
    iNumNodes++;
    switch (divide & 3)
    { // podzielenie jednego z bok�w, powstaje wierzcho�ek D
    case 0: // podzia� AB (0-1) -> ADC i DBC
        ntri->Vertices[2] = node->Vertices[2]; // wierzcho�ek C jest wsp�lny
        ntri->Vertices[1] = node->Vertices[1]; // wierzcho�ek B przechodzi do nowego
        // node->Vertices[1].HalfSet(node->Vertices[0],node->Vertices[1]); //na razie D tak
        if (divide & 4)
            node->Vertices[1].SetByZ(node->Vertices[0], node->Vertices[1], divide & 8 ? z1 : z0);
        else
            node->Vertices[1].SetByX(node->Vertices[0], node->Vertices[1], divide & 8 ? x1 : x0);
        ntri->Vertices[0] = node->Vertices[1]; // wierzcho�ek D jest wsp�lny
        break;
    case 1: // podzia� BC (1-2) -> ABD i ADC
        ntri->Vertices[0] = node->Vertices[0]; // wierzcho�ek A jest wsp�lny
        ntri->Vertices[2] = node->Vertices[2]; // wierzcho�ek C przechodzi do nowego
        // node->Vertices[2].HalfSet(node->Vertices[1],node->Vertices[2]); //na razie D tak
        if (divide & 4)
            node->Vertices[2].SetByZ(node->Vertices[1], node->Vertices[2], divide & 8 ? z1 : z0);
        else
            node->Vertices[2].SetByX(node->Vertices[1], node->Vertices[2], divide & 8 ? x1 : x0);
        ntri->Vertices[1] = node->Vertices[2]; // wierzcho�ek D jest wsp�lny
        break;
    case 2: // podzia� CA (2-0) -> ABD i DBC
        ntri->Vertices[1] = node->Vertices[1]; // wierzcho�ek B jest wsp�lny
        ntri->Vertices[2] = node->Vertices[2]; // wierzcho�ek C przechodzi do nowego
        // node->Vertices[2].HalfSet(node->Vertices[2],node->Vertices[0]); //na razie D tak
        if (divide & 4)
            node->Vertices[2].SetByZ(node->Vertices[2], node->Vertices[0], divide & 8 ? z1 : z0);
        else
            node->Vertices[2].SetByX(node->Vertices[2], node->Vertices[0], divide & 8 ? x1 : x0);
        ntri->Vertices[0] = node->Vertices[2]; // wierzcho�ek D jest wsp�lny
        break;
    }
    // przeliczenie �rodk�w ci�ko�ci obu
    node->pCenter =
        (node->Vertices[0].Point + node->Vertices[1].Point + node->Vertices[2].Point) / 3.0;
    ntri->pCenter =
        (ntri->Vertices[0].Point + ntri->Vertices[1].Point + ntri->Vertices[2].Point) / 3.0;
    RaTriangleDivider(node); // rekurencja, bo nawet na TD raz nie wystarczy
    RaTriangleDivider(ntri);
};

TGroundNode * TGround::AddGroundNode(cParser *parser)
{ // wczytanie wpisu typu "node"
    // parser->LoadTraction=Global::bLoadTraction; //Ra: tu nie potrzeba powtarza�
    AnsiString str, str1, str2, str3, str4, Skin, DriverType, asNodeName;
    int nv, ti, i, n;
    double tf, r, rmin, tf1, tf2, tf3, tf4, l, dist, mgn;
    int int1, int2;
    bool bError = false, curve;
    vector3 pt, front, up, left, pos, tv;
    matrix4x4 mat2, mat1, mat;
    GLuint TexID;
    TGroundNode *tmp1;
    TTrack *Track;
    TTextSound *tmpsound;
    std::string token;
    parser->getTokens(2);
    *parser >> r >> rmin;
    parser->getTokens();
    *parser >> token;
    asNodeName = AnsiString(token.c_str());
    parser->getTokens();
    *parser >> token;
    str = AnsiString(token.c_str());
    TGroundNode *tmp, *tmp2;
    tmp = new TGroundNode();
    tmp->asName = (asNodeName == AnsiString("none") ? AnsiString("") : asNodeName);
    if (r >= 0)
        tmp->fSquareRadius = r * r;
    tmp->fSquareMinRadius = rmin * rmin;
    if (str == "triangles")
        tmp->iType = GL_TRIANGLES;
    else if (str == "triangle_strip")
        tmp->iType = GL_TRIANGLE_STRIP;
    else if (str == "triangle_fan")
        tmp->iType = GL_TRIANGLE_FAN;
    else if (str == "lines")
        tmp->iType = GL_LINES;
    else if (str == "line_strip")
        tmp->iType = GL_LINE_STRIP;
    else if (str == "line_loop")
        tmp->iType = GL_LINE_LOOP;
    else if (str == "model")
        tmp->iType = TP_MODEL;
    // else if (str=="terrain")             tmp->iType=TP_TERRAIN; //tymczasowo do odwo�ania
    else if (str == "dynamic")
        tmp->iType = TP_DYNAMIC;
    else if (str == "sound")
        tmp->iType = TP_SOUND;
    else if (str == "track")
        tmp->iType = TP_TRACK;
    else if (str == "memcell")
        tmp->iType = TP_MEMCELL;
    else if (str == "eventlauncher")
        tmp->iType = TP_EVLAUNCH;
    else if (str == "traction")
        tmp->iType = TP_TRACTION;
    else if (str == "tractionpowersource")
        tmp->iType = TP_TRACTIONPOWERSOURCE;
    // else if (str=="isolated")            tmp->iType=TP_ISOLATED;
    else
        bError = true;
    // WriteLog("-> node "+str+" "+tmp->asName);
    if (bError)
    {
        Error(AnsiString("Scene parse error near " + str).c_str());
        for (int i = 0; i < 60; ++i)
        { // Ra: skopiowanie dalszej cz�ci do logu - taka prowizorka, lepsza ni� nic
            parser->getTokens(); // pobranie linijki tekstu nie dzia�a
            *parser >> token;
            WriteLog(token.c_str());
        }
        // if (tmp==RootNode) RootNode=NULL;
        delete tmp;
        return NULL;
    }
    switch (tmp->iType)
    {
    case TP_TRACTION:
        tmp->hvTraction = new TTraction();
        parser->getTokens();
        *parser >> token;
        tmp->hvTraction->asPowerSupplyName = AnsiString(token.c_str()); // nazwa zasilacza
        parser->getTokens(3);
        *parser >> tmp->hvTraction->NominalVoltage >> tmp->hvTraction->MaxCurrent >>
            tmp->hvTraction->fResistivity;
        if (tmp->hvTraction->fResistivity == 0.01) // tyle jest w sceneriach [om/km]
            tmp->hvTraction->fResistivity = 0.075; // taka sensowniejsza warto�� za
        // http://www.ikolej.pl/fileadmin/user_upload/Seminaria_IK/13_05_07_Prezentacja_Kruczek.pdf
        tmp->hvTraction->fResistivity *= 0.001; // teraz [om/m]
        parser->getTokens();
        *parser >> token;
        // Ra 2014-02: a tutaj damy symbol sieci i jej budow�, np.:
        // SKB70-C, CuCd70-2C, KB95-2C, C95-C, C95-2C, YC95-2C, YpC95-2C, YC120-2C
        // YpC120-2C, YzC120-2C, YwsC120-2C, YC150-C150, YC150-2C150, C150-C150
        // C120-2C, 2C120-2C, 2C120-2C-1, 2C120-2C-2, 2C120-2C-3, 2C120-2C-4
        if (token.compare("none") == 0)
            tmp->hvTraction->Material = 0;
        else if (token.compare("al") == 0)
            tmp->hvTraction->Material = 2; // 1=aluminiowa, rysuje si� na czarno
        else
            tmp->hvTraction->Material = 1; // 1=miedziana, rysuje si� na zielono albo czerwono
        parser->getTokens();
        *parser >> tmp->hvTraction->WireThickness;
        parser->getTokens();
        *parser >> tmp->hvTraction->DamageFlag;
        parser->getTokens(3);
        *parser >> tmp->hvTraction->pPoint1.x >> tmp->hvTraction->pPoint1.y >>
            tmp->hvTraction->pPoint1.z;
        tmp->hvTraction->pPoint1 += pOrigin;
        parser->getTokens(3);
        *parser >> tmp->hvTraction->pPoint2.x >> tmp->hvTraction->pPoint2.y >>
            tmp->hvTraction->pPoint2.z;
        tmp->hvTraction->pPoint2 += pOrigin;
        parser->getTokens(3);
        *parser >> tmp->hvTraction->pPoint3.x >> tmp->hvTraction->pPoint3.y >>
            tmp->hvTraction->pPoint3.z;
        tmp->hvTraction->pPoint3 += pOrigin;
        parser->getTokens(3);
        *parser >> tmp->hvTraction->pPoint4.x >> tmp->hvTraction->pPoint4.y >>
            tmp->hvTraction->pPoint4.z;
        tmp->hvTraction->pPoint4 += pOrigin;
        parser->getTokens();
        *parser >> tf1;
        tmp->hvTraction->fHeightDifference =
            (tmp->hvTraction->pPoint3.y - tmp->hvTraction->pPoint1.y + tmp->hvTraction->pPoint4.y -
             tmp->hvTraction->pPoint2.y) *
                0.5f -
            tf1;
        parser->getTokens();
        *parser >> tf1;
        if (tf1 > 0)
            tmp->hvTraction->iNumSections =
                (tmp->hvTraction->pPoint1 - tmp->hvTraction->pPoint2).Length() / tf1;
        else
            tmp->hvTraction->iNumSections = 0;
        parser->getTokens();
        *parser >> tmp->hvTraction->Wires;
        parser->getTokens();
        *parser >> tmp->hvTraction->WireOffset;
        parser->getTokens();
        *parser >> token;
        tmp->bVisible = (token.compare("vis") == 0);
        parser->getTokens();
        *parser >> token;
        if (token.compare("parallel") == 0)
        { // jawne wskazanie innego prz�s�a, na kt�re mo�e przestawi� si� pantograf
            parser->getTokens();
            *parser >> token; // wypada�o by to zapami�ta�...
            tmp->hvTraction->asParallel = AnsiString(token.c_str());
            parser->getTokens();
            *parser >> token; // a tu ju� powinien by� koniec
        }
        if (token.compare("endtraction") != 0)
            Error("ENDTRACTION delimiter missing! " + str2 + " found instead.");
        tmp->hvTraction->Init(); // przeliczenie parametr�w
        // if (Global::bLoadTraction)
        // tmp->hvTraction->Optimize(); //generowanie DL dla wszystkiego przy wczytywaniu?
        tmp->pCenter = (tmp->hvTraction->pPoint2 + tmp->hvTraction->pPoint1) * 0.5f;
        // if (!Global::bLoadTraction) SafeDelete(tmp); //Ra: tak by� nie mo�e, bo NULL to b��d
        break;
    case TP_TRACTIONPOWERSOURCE:
        parser->getTokens(3);
        *parser >> tmp->pCenter.x >> tmp->pCenter.y >> tmp->pCenter.z;
        tmp->pCenter += pOrigin;
        tmp->psTractionPowerSource = new TTractionPowerSource(tmp);
        tmp->psTractionPowerSource->Load(parser);
        break;
    case TP_MEMCELL:
        parser->getTokens(3);
        *parser >> tmp->pCenter.x >> tmp->pCenter.y >> tmp->pCenter.z;
        tmp->pCenter.RotateY(aRotate.y / 180.0 * M_PI); // Ra 2014-11: uwzgl�dnienie rotacji
        tmp->pCenter += pOrigin;
        tmp->MemCell = new TMemCell(&tmp->pCenter);
        tmp->MemCell->Load(parser);
        if (!tmp->asName.IsEmpty()) // jest pusta gdy "none"
        { // dodanie do wyszukiwarki
            if (sTracks->Update(TP_MEMCELL, tmp->asName.c_str(),
                                tmp)) // najpierw sprawdzi�, czy ju� jest
            { // przy zdublowaniu wska�nik zostanie podmieniony w drzewku na p�niejszy (zgodno��
                // wsteczna)
                ErrorLog("Duplicated memcell: " + tmp->asName); // to zg�asza� duplikat
            }
            else
                sTracks->Add(TP_MEMCELL, tmp->asName.c_str(), tmp); // nazwa jest unikalna
        }
        break;
    case TP_EVLAUNCH:
        parser->getTokens(3);
        *parser >> tmp->pCenter.x >> tmp->pCenter.y >> tmp->pCenter.z;
        tmp->pCenter.RotateY(aRotate.y / 180.0 * M_PI); // Ra 2014-11: uwzgl�dnienie rotacji
        tmp->pCenter += pOrigin;
        tmp->EvLaunch = new TEventLauncher();
        tmp->EvLaunch->Load(parser);
        break;
    case TP_TRACK:
        tmp->pTrack = new TTrack(tmp);
        if (Global::iWriteLogEnabled & 4)
            if (!tmp->asName.IsEmpty())
                WriteLog(tmp->asName.c_str());
        tmp->pTrack->Load(parser, pOrigin,
                          tmp->asName); // w nazwie mo�e by� nazwa odcinka izolowanego
        if (!tmp->asName.IsEmpty()) // jest pusta gdy "none"
        { // dodanie do wyszukiwarki
            if (sTracks->Update(TP_TRACK, tmp->asName.c_str(),
                                tmp)) // najpierw sprawdzi�, czy ju� jest
            { // przy zdublowaniu wska�nik zostanie podmieniony w drzewku na p�niejszy (zgodno��
                // wsteczna)
                if (tmp->pTrack->iCategoryFlag & 1) // je�li jest zdublowany tor kolejowy
                    ErrorLog("Duplicated track: " + tmp->asName); // to zg�asza� duplikat
            }
            else
                sTracks->Add(TP_TRACK, tmp->asName.c_str(), tmp); // nazwa jest unikalna
        }
        tmp->pCenter = (tmp->pTrack->CurrentSegment()->FastGetPoint_0() +
                        tmp->pTrack->CurrentSegment()->FastGetPoint(0.5) +
                        tmp->pTrack->CurrentSegment()->FastGetPoint_1()) /
                       3.0;
        break;
    case TP_SOUND:
        tmp->tsStaticSound = new TTextSound;
        parser->getTokens(3);
        *parser >> tmp->pCenter.x >> tmp->pCenter.y >> tmp->pCenter.z;
        tmp->pCenter.RotateY(aRotate.y / 180.0 * M_PI); // Ra 2014-11: uwzgl�dnienie rotacji
        tmp->pCenter += pOrigin;
        parser->getTokens();
        *parser >> token;
        str = AnsiString(token.c_str());
        tmp->tsStaticSound->Init(str.c_str(), sqrt(tmp->fSquareRadius), tmp->pCenter.x,
                                 tmp->pCenter.y, tmp->pCenter.z, false, rmin);
        if (rmin < 0.0)
            rmin =
                0.0; // przywr�cenie poprawnej warto�ci, je�li s�u�y�a do wy��czenia efektu Dopplera

        //            tmp->pDirectSoundBuffer=TSoundsManager::GetFromName(str.c_str());
        //            tmp->iState=(Parser->GetNextSymbol().LowerCase()=="loop"?DSBPLAY_LOOPING:0);
        parser->getTokens();
        *parser >> token;
        break;
    case TP_DYNAMIC:
        tmp->DynamicObject = new TDynamicObject();
        // tmp->DynamicObject->Load(Parser);
        parser->getTokens();
        *parser >> token;
        str1 = AnsiString(token.c_str()); // katalog
        // McZapkie: doszedl parametr ze zmienialna skora
        parser->getTokens();
        *parser >> token;
        Skin = AnsiString(token.c_str()); // tekstura wymienna
        parser->getTokens();
        *parser >> token;
        str3 = AnsiString(token.c_str()); // McZapkie-131102: model w MMD
        if (bTrainSet)
        { // je�li pojazd jest umieszczony w sk�adzie
            str = asTrainSetTrack;
            parser->getTokens();
            *parser >> tf1; // Ra: -1 oznacza odwrotne wstawienie, normalnie w sk�adzie 0
            parser->getTokens();
            *parser >> token;
            DriverType = AnsiString(token.c_str()); // McZapkie:010303 - w przyszlosci rozne
            // konfiguracje mechanik/pomocnik itp
            tf3 = fTrainSetVel; // pr�dko��
            parser->getTokens();
            *parser >> token;
            str4 = AnsiString(token.c_str());
            int2 = str4.Pos("."); // yB: wykorzystuje tutaj zmienna, ktora potem bedzie ladunkiem
            if (int2 > 0) // yB: jesli znalazl kropke, to ja przetwarza jako parametry
            {
                int dlugosc = str4.Length();
                int1 = str4.SubString(1, int2 - 1).ToInt(); // niech sprzegiem bedzie do kropki cos
                str4 = str4.SubString(int2 + 1, dlugosc - int2);
            }
            else
            {
                int1 = str4.ToInt();
                str4 = "";
            }
            int2 = 0; // zeruje po wykorzystaniu
            //    *parser >> int1; //yB: nastawy i takie tam TUTAJ!!!!!
            if (int1 < 0)
                int1 = (-int1) |
                       ctrain_depot; // sprz�g zablokowany (pojazdy nieroz��czalne przy manewrach)
            if (tf1 != -1.0)
                if (fabs(tf1) > 0.5) // maksymalna odleg�o�� mi�dzy sprz�gami - do przemy�lenia
                    int1 = 0; // likwidacja sprz�gu, je�li odleg�o�� zbyt du�a - to powinno by�
            // uwzgl�dniane w fizyce sprz�g�w...
            TempConnectionType[iTrainSetWehicleNumber] = int1; // warto�� dodatnia
        }
        else
        { // pojazd wstawiony luzem
            fTrainSetDist = 0; // zerowanie dodatkowego przesuni�cia
            asTrainName = ""; // puste oznacza jazd� pojedynczego bez rozk�adu, "none" jest dla
            // sk�adu (trainset)
            parser->getTokens();
            *parser >> token;
            str = AnsiString(token.c_str()); // track
            parser->getTokens();
            *parser >> tf1; // Ra: -1 oznacza odwrotne wstawienie
            parser->getTokens();
            *parser >> token;
            DriverType = AnsiString(token.c_str()); // McZapkie:010303: obsada
            parser->getTokens();
            *parser >>
                tf3; // pr�dko��, niekt�rzy wpisuj� tu "3" jako sprz�g, �eby nie by�o tabliczki
            iTrainSetWehicleNumber = 0;
            TempConnectionType[iTrainSetWehicleNumber] = 3; // likwidacja tabliczki na ko�cu?
        }
        parser->getTokens();
        *parser >> int2; // ilo�� �adunku
        if (int2 > 0)
        { // je�eli �adunku jest wi�cej ni� 0, to rozpoznajemy jego typ
            parser->getTokens();
            *parser >> token;
            str2 = AnsiString(token.c_str()); // LoadType
            if (str2 == AnsiString("enddynamic")) // idiotoodporno��: �adunek bez podanego typu
            {
                str2 = "";
                int2 = 0; // ilo�� bez typu si� nie liczy jako �adunek
            }
        }
        else
            str2 = ""; // brak ladunku

        tmp1 = FindGroundNode(str, TP_TRACK); // poszukiwanie toru
        if (tmp1 ? tmp1->pTrack != NULL : false)
        { // je�li tor znaleziony
            Track = tmp1->pTrack;
            if (!iTrainSetWehicleNumber) // je�li pierwszy pojazd
                if (Track->evEvent0) // je�li tor ma Event0
                    if (fabs(fTrainSetVel) <= 1.0) // a sk�ad stoi
                        if (fTrainSetDist >= 0.0) // ale mo�e nie si�ga� na owy tor
                            if (fTrainSetDist < 8.0) // i raczej nie si�ga
                                fTrainSetDist =
                                    8.0; // przesuwamy oko�o p� EU07 dla wstecznej zgodno�ci
            // WriteLog("Dynamic shift: "+AnsiString(fTrainSetDist));
            /* //Ra: to jednak robi du�e problemy - przesuni�cie w dynamic jest przesuni�ciem do
               ty�u, odwrotnie ni� w trainset
                if (!iTrainSetWehicleNumber) //dla pierwszego jest to przesuni�cie (ujemne = do
               ty�u)
                 if (tf1!=-1.0) //-1 wyj�tkowo oznacza odwr�cenie
                  tf1=-tf1; //a dla kolejnych odleg�o�� mi�dzy sprz�gami (ujemne = wbite)
            */
            tf3 = tmp->DynamicObject->Init(asNodeName, str1, Skin, str3, Track,
                                           (tf1 == -1.0 ? fTrainSetDist : fTrainSetDist - tf1),
                                           DriverType, tf3, asTrainName, int2, str2, (tf1 == -1.0),
                                           str4);
            if (tf3 != 0.0) // zero oznacza b��d
            {
                fTrainSetDist -=
                    tf3; // przesuni�cie dla kolejnego, minus bo idziemy w stron� punktu 1
                tmp->pCenter = tmp->DynamicObject->GetPosition();
                if (TempConnectionType[iTrainSetWehicleNumber]) // je�li jest sprz�g
                    if (tmp->DynamicObject->MoverParameters->Couplers[tf1 == -1.0 ? 0 : 1]
                            .AllowedFlag &
                        ctrain_depot) // jesli zablokowany
                        TempConnectionType[iTrainSetWehicleNumber] |= ctrain_depot; // b�dzie
                // blokada
                iTrainSetWehicleNumber++;
            }
            else
            { // LastNode=NULL;
                delete tmp;
                tmp = NULL; // nie mo�e by� tu return, bo trzeba pomin�� jeszcze enddynamic
            }
        }
        else
        { // gdy tor nie znaleziony
            ErrorLog("Missed track: dynamic placed on \"" + tmp->DynamicObject->asTrack + "\"");
            delete tmp;
            tmp = NULL; // nie mo�e by� tu return, bo trzeba pomin�� jeszcze enddynamic
        }
        parser->getTokens();
        *parser >> token;
        if (token.compare("destination") == 0)
        { // dok�d wagon ma jecha�, uwzgl�dniane przy manewrach
            parser->getTokens();
            *parser >> token;
            if (tmp)
                tmp->DynamicObject->asDestination = AnsiString(token.c_str());
            *parser >> token;
        }
        if (token.compare("enddynamic") != 0)
            Error("enddynamic statement missing");
        break;
    case TP_MODEL:
        if (rmin < 0)
        {
            tmp->iType = TP_TERRAIN;
            tmp->fSquareMinRadius = 0; // to w og�le potrzebne?
        }
        parser->getTokens(3);
        *parser >> tmp->pCenter.x >> tmp->pCenter.y >> tmp->pCenter.z;
        parser->getTokens();
        *parser >> tf1;
        // OlO_EU&KAKISH-030103: obracanie punktow zaczepien w modelu
        tmp->pCenter.RotateY(aRotate.y / 180.0 * M_PI);
        // McZapkie-260402: model tez ma wspolrzedne wzgledne
        tmp->pCenter += pOrigin;
        // tmp->fAngle+=aRotate.y; // /180*M_PI
        /*
           if (tmp->iType==TP_MODEL)
           {//je�li standardowy model
        */
        tmp->Model = new TAnimModel();
        tmp->Model->RaAnglesSet(aRotate.x, tf1 + aRotate.y,
                                aRotate.z); // dostosowanie do pochylania linii
        if (tmp->Model->Load(
                parser, tmp->iType == TP_TERRAIN)) // wczytanie modelu, tekstury i stanu �wiate�...
            tmp->iFlags =
                tmp->Model->Flags() | 0x200; // ustalenie, czy przezroczysty; flaga usuwania
        else if (tmp->iType != TP_TERRAIN)
        { // model nie wczyta� si� - ignorowanie node
            delete tmp;
            tmp = NULL; // nie mo�e by� tu return
            break; // nie mo�e by� tu return?
        }
        /*
           }
           else if (tmp->iType==TP_TERRAIN)
           {//nie potrzeba nak�adki animuj�cej submodele
            *parser >> token;
            tmp->pModel3D=TModelsManager::GetModel(token.c_str(),false);
            do //Ra: z tym to troch� bez sensu jest
            {parser->getTokens();
             *parser >> token;
             str=AnsiString(token.c_str());
            } while (str!="endterrains");
           }
        */
        if (tmp->iType == TP_TERRAIN)
        { // je�li model jest terenem, trzeba utworzy� dodatkowe obiekty
            // po wczytaniu model ma ju� utworzone DL albo VBO
            Global::pTerrainCompact = tmp->Model; // istnieje co najmniej jeden obiekt terenu
            tmp->iCount = Global::pTerrainCompact->TerrainCount() + 1; // zliczenie submodeli
            tmp->nNode = new TGroundNode[tmp->iCount]; // sztuczne node dla kwadrat�w
            tmp->nNode[0].iType = TP_MODEL; // pierwszy zawiera model (dla delete)
            tmp->nNode[0].Model = Global::pTerrainCompact;
            tmp->nNode[0].iFlags = 0x200; // nie wy�wietlany, ale usuwany
            for (i = 1; i < tmp->iCount; ++i)
            { // a reszta to submodele
                tmp->nNode[i].iType = TP_SUBMODEL; //
                tmp->nNode[i].smTerrain = Global::pTerrainCompact->TerrainSquare(i - 1);
                tmp->nNode[i].iFlags = 0x10; // nieprzezroczyste; nie usuwany
                tmp->nNode[i].bVisible = true;
                tmp->nNode[i].pCenter = tmp->pCenter; // nie przesuwamy w inne miejsce
                // tmp->nNode[i].asName=
            }
        }
        else if (!tmp->asName.IsEmpty()) // jest pusta gdy "none"
        { // dodanie do wyszukiwarki
            if (sTracks->Update(TP_MODEL, tmp->asName.c_str(),
                                tmp)) // najpierw sprawdzi�, czy ju� jest
            { // przy zdublowaniu wska�nik zostanie podmieniony w drzewku na p�niejszy (zgodno��
                // wsteczna)
                ErrorLog("Duplicated model: " + tmp->asName); // to zg�asza� duplikat
            }
            else
                sTracks->Add(TP_MODEL, tmp->asName.c_str(), tmp); // nazwa jest unikalna
        }
        // str=Parser->GetNextSymbol().LowerCase();
        break;
    // case TP_GEOMETRY :
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        parser->getTokens();
        *parser >> token;
        // McZapkie-050702: opcjonalne wczytywanie parametrow materialu (ambient,diffuse,specular)
        if (token.compare("material") == 0)
        {
            parser->getTokens();
            *parser >> token;
            while (token.compare("endmaterial") != 0)
            {
                if (token.compare("ambient:") == 0)
                {
                    parser->getTokens();
                    *parser >> tmp->Ambient[0];
                    parser->getTokens();
                    *parser >> tmp->Ambient[1];
                    parser->getTokens();
                    *parser >> tmp->Ambient[2];
                }
                else if (token.compare("diffuse:") == 0)
                { // Ra: co� jest nie tak, bo w jednej linijce nie dzia�a
                    parser->getTokens();
                    *parser >> tmp->Diffuse[0];
                    parser->getTokens();
                    *parser >> tmp->Diffuse[1];
                    parser->getTokens();
                    *parser >> tmp->Diffuse[2];
                }
                else if (token.compare("specular:") == 0)
                {
                    parser->getTokens();
                    *parser >> tmp->Specular[0];
                    parser->getTokens();
                    *parser >> tmp->Specular[1];
                    parser->getTokens();
                    *parser >> tmp->Specular[2];
                }
                else
                    Error("Scene material failure!");
                parser->getTokens();
                *parser >> token;
            }
        }
        if (token.compare("endmaterial") == 0)
        {
            parser->getTokens();
            *parser >> token;
        }
        str = AnsiString(token.c_str());
#ifdef _PROBLEND
        // PROBLEND Q: 13122011 - Szociu: 27012012
        PROBLEND = true; // domyslnie uruchomione nowe wy�wietlanie
        tmp->PROBLEND = true; // odwolanie do tgroundnode, bo rendering jest w tej klasie
        if (str.Pos("@") > 0) // sprawdza, czy w nazwie tekstury jest znak "@"
        {
            PROBLEND = false; // je�li jest, wyswietla po staremu
            tmp->PROBLEND = false;
        }
#endif
        tmp->TextureID = TTexturesManager::GetTextureID(szTexturePath, szSceneryPath, str.c_str());
        tmp->iFlags = TTexturesManager::GetAlpha(tmp->TextureID) ? 0x220 : 0x210; // z usuwaniem
        if (((tmp->iType == GL_TRIANGLES) && (tmp->iFlags & 0x10)) ?
                Global::pTerrainCompact->TerrainLoaded() :
                false)
        { // je�li jest tekstura nieprzezroczysta, a teren za�adowany, to pomijamy tr�jk�ty
            do
            { // pomijanie tr�jk�t�w
                parser->getTokens();
                *parser >> token;
            } while (token.compare("endtri") != 0);
            // delete tmp; //nie ma co tego trzyma�
            // tmp=NULL; //to jest b��d
        }
        else
        {
            i = 0;
            do
            {
                if (i < 9999) // 3333 tr�jk�ty
                { // liczba wierzcho�k�w nie jest nieograniczona
                    parser->getTokens(3);
                    *parser >> TempVerts[i].Point.x >> TempVerts[i].Point.y >> TempVerts[i].Point.z;
                    parser->getTokens(3);
                    *parser >> TempVerts[i].Normal.x >> TempVerts[i].Normal.y >>
                        TempVerts[i].Normal.z;
                    /*
                         str=Parser->GetNextSymbol().LowerCase();
                         if (str==AnsiString("x"))
                             TempVerts[i].tu=(TempVerts[i].Point.x+Parser->GetNextSymbol().ToDouble())/Parser->GetNextSymbol().ToDouble();
                         else
                         if (str==AnsiString("y"))
                             TempVerts[i].tu=(TempVerts[i].Point.y+Parser->GetNextSymbol().ToDouble())/Parser->GetNextSymbol().ToDouble();
                         else
                         if (str==AnsiString("z"))
                             TempVerts[i].tu=(TempVerts[i].Point.z+Parser->GetNextSymbol().ToDouble())/Parser->GetNextSymbol().ToDouble();
                         else
                             TempVerts[i].tu=str.ToDouble();;

                         str=Parser->GetNextSymbol().LowerCase();
                         if (str==AnsiString("x"))
                             TempVerts[i].tv=(TempVerts[i].Point.x+Parser->GetNextSymbol().ToDouble())/Parser->GetNextSymbol().ToDouble();
                         else
                         if (str==AnsiString("y"))
                             TempVerts[i].tv=(TempVerts[i].Point.y+Parser->GetNextSymbol().ToDouble())/Parser->GetNextSymbol().ToDouble();
                         else
                         if (str==AnsiString("z"))
                             TempVerts[i].tv=(TempVerts[i].Point.z+Parser->GetNextSymbol().ToDouble())/Parser->GetNextSymbol().ToDouble();
                         else
                             TempVerts[i].tv=str.ToDouble();;
                    */
                    parser->getTokens(2);
                    *parser >> TempVerts[i].tu >> TempVerts[i].tv;

                    //    tf=Parser->GetNextSymbol().ToDouble();
                    //          TempVerts[i].tu=tf;
                    //        tf=Parser->GetNextSymbol().ToDouble();
                    //      TempVerts[i].tv=tf;

                    TempVerts[i].Point.RotateZ(aRotate.z / 180 * M_PI);
                    TempVerts[i].Point.RotateX(aRotate.x / 180 * M_PI);
                    TempVerts[i].Point.RotateY(aRotate.y / 180 * M_PI);
                    TempVerts[i].Normal.RotateZ(aRotate.z / 180 * M_PI);
                    TempVerts[i].Normal.RotateX(aRotate.x / 180 * M_PI);
                    TempVerts[i].Normal.RotateY(aRotate.y / 180 * M_PI);
                    TempVerts[i].Point += pOrigin;
                    tmp->pCenter += TempVerts[i].Point;
                }
                else if (i == 9999)
                    ErrorLog("Bad triangles: too many verices");
                i++;
                parser->getTokens();
                *parser >> token;

                //   }

            } while (token.compare("endtri") != 0);
            nv = i;
            tmp->Init(nv); // utworzenie tablicy wierzcho�k�w
            tmp->pCenter /= (nv > 0 ? nv : 1);

            //   memcpy(tmp->Vertices,TempVerts,nv*sizeof(TGroundVertex));

            r = 0;
            for (int i = 0; i < nv; i++)
            {
                tmp->Vertices[i] = TempVerts[i];
                tf = SquareMagnitude(tmp->Vertices[i].Point - tmp->pCenter);
                if (tf > r)
                    r = tf;
            }

            //   tmp->fSquareRadius=2000*2000+r;
            tmp->fSquareRadius += r;
            RaTriangleDivider(tmp); // Ra: dzielenie tr�jk�t�w jest teraz ca�kiem wydajne
        } // koniec wczytywania tr�jk�t�w
        break;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        parser->getTokens(3);
        *parser >> tmp->Diffuse[0] >> tmp->Diffuse[1] >> tmp->Diffuse[2];
        //   tmp->Diffuse[0]=Parser->GetNextSymbol().ToDouble()/255;
        //   tmp->Diffuse[1]=Parser->GetNextSymbol().ToDouble()/255;
        //   tmp->Diffuse[2]=Parser->GetNextSymbol().ToDouble()/255;
        parser->getTokens();
        *parser >> tmp->fLineThickness;
        i = 0;
        parser->getTokens();
        *parser >> token;
        do
        {
            str = AnsiString(token.c_str());
            TempVerts[i].Point.x = str.ToDouble();
            parser->getTokens(2);
            *parser >> TempVerts[i].Point.y >> TempVerts[i].Point.z;
            TempVerts[i].Point.RotateZ(aRotate.z / 180 * M_PI);
            TempVerts[i].Point.RotateX(aRotate.x / 180 * M_PI);
            TempVerts[i].Point.RotateY(aRotate.y / 180 * M_PI);
            TempVerts[i].Point += pOrigin;
            tmp->pCenter += TempVerts[i].Point;
            i++;
            parser->getTokens();
            *parser >> token;
        } while (token.compare("endline") != 0);
        nv = i;
        //   tmp->Init(nv);
        tmp->Points = new vector3[nv];
        tmp->iNumPts = nv;
        tmp->pCenter /= (nv > 0 ? nv : 1);
        for (int i = 0; i < nv; i++)
            tmp->Points[i] = TempVerts[i].Point;
        break;
    }
    return tmp;
}

TSubRect * TGround::FastGetSubRect(int iCol, int iRow)
{
    int br, bc, sr, sc;
    br = iRow / iNumSubRects;
    bc = iCol / iNumSubRects;
    sr = iRow - br * iNumSubRects;
    sc = iCol - bc * iNumSubRects;
    if ((br < 0) || (bc < 0) || (br >= iNumRects) || (bc >= iNumRects))
        return NULL;
    return (Rects[br][bc].FastGetRect(sc, sr));
}

TSubRect * TGround::GetSubRect(int iCol, int iRow)
{ // znalezienie ma�ego kwadratu mapy
    int br, bc, sr, sc;
    br = iRow / iNumSubRects; // wsp�rz�dne kwadratu kilometrowego
    bc = iCol / iNumSubRects;
    sr = iRow - br * iNumSubRects; // wsp�rz�dne wzgl�ne ma�ego kwadratu
    sc = iCol - bc * iNumSubRects;
    if ((br < 0) || (bc < 0) || (br >= iNumRects) || (bc >= iNumRects))
        return NULL; // je�li poza map�
    return (Rects[br][bc].SafeGetRect(sc, sr)); // pobranie ma�ego kwadratu
}

TEvent * TGround::FindEvent(const AnsiString &asEventName)
{
    return (TEvent *)sTracks->Find(0, asEventName.c_str()); // wyszukiwanie w drzewie
    /* //powolna wyszukiwarka
     for (TEvent *Current=RootEvent;Current;Current=Current->Next2)
     {
      if (Current->asName==asEventName)
       return Current;
     }
     return NULL;
    */
}

TEvent * TGround::FindEventScan(const AnsiString &asEventName)
{ // wyszukanie eventu z opcj� utworzenia niejawnego dla kom�rek skanowanych
    TEvent *e = (TEvent *)sTracks->Find(0, asEventName.c_str()); // wyszukiwanie w drzewie event�w
    if (e)
        return e; // jak istnieje, to w porz�dku
    if (asEventName.SubString(asEventName.Length() - 4, 5) ==
        ":scan") // jeszcze mo�e by� event niejawny
    { // no to szukamy kom�rki pami�ci o nazwie zawartej w evencie
        AnsiString n = asEventName.SubString(1, asEventName.Length() - 5); // do dwukropka
        if (sTracks->Find(TP_MEMCELL, n.c_str())) // je�li jest takowa kom�rka pami�ci
            e = new TEvent(n); // utworzenie niejawnego eventu jej odczytu
    }
    return e; // utworzony albo si� nie uda�o
}

void TGround::FirstInit()
{ // ustalanie zale�no�ci na scenerii przed wczytaniem pojazd�w
    if (bInitDone)
        return; // Ra: �eby nie robi�o si� dwa razy
    bInitDone = true;
    WriteLog("InitNormals");
    int i, j;
    for (i = 0; i < TP_LAST; ++i)
    {
        for (TGroundNode *Current = nRootOfType[i]; Current; Current = Current->nNext)
        {
            Current->InitNormals();
            if (Current->iType != TP_DYNAMIC)
            { // pojazd�w w og�le nie dotyczy dodawanie do mapy
                if (i == TP_EVLAUNCH ? Current->EvLaunch->IsGlobal() : false)
                    srGlobal.NodeAdd(Current); // dodanie do globalnego obiektu
                else if (i == TP_TERRAIN)
                { // specjalne przetwarzanie terenu wczytanego z pliku E3D
                    AnsiString xxxzzz; // nazwa kwadratu
                    TGroundRect *gr;
                    for (j = 1; j < Current->iCount; ++j)
                    { // od 1 do ko�ca s� zestawy tr�jk�t�w
                        xxxzzz = AnsiString(Current->nNode[j].smTerrain->pName); // pobranie nazwy
                        gr = GetRect(1000 * (xxxzzz.SubString(1, 3).ToIntDef(0) - 500),
                                     1000 * (xxxzzz.SubString(4, 3).ToIntDef(0) - 500));
                        if (Global::bUseVBO)
                            gr->nTerrain = Current->nNode + j; // zapami�tanie
                        else
                            gr->RaNodeAdd(&Current->nNode[j]);
                    }
                }
                //    else if
                //    ((Current->iType!=GL_TRIANGLES)&&(Current->iType!=GL_TRIANGLE_STRIP)?true
                //    //~czy tr�jk�t?
                else if ((Current->iType != GL_TRIANGLES) ?
                             true //~czy tr�jk�t?
                             :
                             (Current->iFlags & 0x20) ?
                             true //~czy tekstur� ma nieprzezroczyst�?
                             :
                             (Current->fSquareMinRadius != 0.0) ?
                             true //~czy widoczny z bliska?
                             :
                             (Current->fSquareRadius <= 90000.0)) //~czy widoczny z daleka?
                    GetSubRect(Current->pCenter.x, Current->pCenter.z)->NodeAdd(Current);
                else // dodajemy do kwadratu kilometrowego
                    GetRect(Current->pCenter.x, Current->pCenter.z)->NodeAdd(Current);
            }
            // if (Current->iType!=TP_DYNAMIC)
            // GetSubRect(Current->pCenter.x,Current->pCenter.z)->AddNode(Current);
        }
    }
    for (i = 0; i < iNumRects; ++i)
        for (j = 0; j < iNumRects; ++j)
            Rects[i][j].Optimize(); // optymalizacja obiekt�w w sektorach
    WriteLog("InitNormals OK");
    WriteLog("InitTracks");
    InitTracks(); //��czenie odcink�w ze sob� i przyklejanie event�w
    WriteLog("InitTracks OK");
    WriteLog("InitTraction");
    InitTraction(); //��czenie drut�w ze sob�
    WriteLog("InitTraction OK");
    WriteLog("InitEvents");
    InitEvents();
    WriteLog("InitEvents OK");
    WriteLog("InitLaunchers");
    InitLaunchers();
    WriteLog("InitLaunchers OK");
    WriteLog("InitGlobalTime");
    // ABu 160205: juz nie TODO :)
    GlobalTime = new TMTableTime(
        hh, mm, srh, srm, ssh,
        ssm); // McZapkie-300302: inicjacja czasu rozkladowego - TODO: czytac z trasy!
    WriteLog("InitGlobalTime OK");
    // jeszcze ustawienie pogody, gdyby nie by�o w scenerii wpis�w
    glClearColor(Global::AtmoColor[0], Global::AtmoColor[1], Global::AtmoColor[2],
                 0.0); // Background Color
    if (Global::fFogEnd > 0)
    {
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, Global::FogColor); // set fog color
        glFogf(GL_FOG_START, Global::fFogStart); // fog start depth
        glFogf(GL_FOG_END, Global::fFogEnd); // fog end depth
        glEnable(GL_FOG);
    }
    else
        glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glLightfv(GL_LIGHT0, GL_POSITION, Global::lightPos); // daylight position
    glLightfv(GL_LIGHT0, GL_AMBIENT, Global::ambientDayLight); // kolor wszechobceny
    glLightfv(GL_LIGHT0, GL_DIFFUSE, Global::diffuseDayLight); // kolor padaj�cy
    glLightfv(GL_LIGHT0, GL_SPECULAR, Global::specularDayLight); // kolor odbity
    // musi by� tutaj, bo wcze�niej nie mieli�my warto�ci �wiat�a
    if (Global::fMoveLight >= 0.0) // albo tak, albo niech ustala minimum ciemno�ci w nocy
    {
        Global::fLuminance = // obliczenie luminacji "�wiat�a w ciemno�ci"
            +0.150 * Global::ambientDayLight[0] // R
            + 0.295 * Global::ambientDayLight[1] // G
            + 0.055 * Global::ambientDayLight[2]; // B
        if (Global::fLuminance > 0.1) // je�li mia�o by by� za jasno
            for (int i = 0; i < 3; i++)
                Global::ambientDayLight[i] *=
                    0.1 / Global::fLuminance; // ograniczenie jasno�ci w nocy
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, Global::ambientDayLight);
    }
    else if (Global::bDoubleAmbient) // Ra: wcze�niej by�o ambient dawane na obydwa �wiat�a
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, Global::ambientDayLight);
    glEnable(GL_LIGHTING);
    WriteLog("FirstInit is done");
};

bool TGround::Init(AnsiString asFile, HDC hDC)
{ // g��wne wczytywanie scenerii
    if (asFile.LowerCase().SubString(1, 7) == "scenery")
        asFile.Delete(1, 8); // Ra: usuni�cie niepotrzebnych znak�w - zgodno�� wstecz z 2003
    WriteLog("Loading scenery from " + asFile);
    Global::pGround = this;
    // pTrain=NULL;
    pOrigin = aRotate = vector3(0, 0, 0); // zerowanie przesuni�cia i obrotu
    AnsiString str = "";
    // TFileStream *fs;
    // int size;
    std::string subpath = Global::asCurrentSceneryPath.c_str(); //   "scenery/";
    cParser parser(asFile.c_str(), cParser::buffer_FILE, subpath, Global::bLoadTraction);
    std::string token;

    /*
        TFileStream *fs;
        fs=new TFileStream(asFile , fmOpenRead	| fmShareCompat	);
        AnsiString str="";
        int size=fs->Size;
        str.SetLength(size);
        fs->Read(str.c_str(),size);
        str+="";
        delete fs;
        TQueryParserComp *Parser;
        Parser=new TQueryParserComp(NULL);
        Parser->TextToParse=str;
    //    Parser->LoadStringToParse(asFile);
        Parser->First();
        AnsiString Token,asFileName;
    */
    const int OriginStackMaxDepth = 100; // rozmiar stosu dla zagnie�d�enia origin
    int OriginStackTop = 0;
    vector3 OriginStack[OriginStackMaxDepth]; // stos zagnie�d�enia origin

    double tf;
    int ParamCount, ParamPos;

    // ABu: Jezeli nie ma definicji w scenerii to ustawiane ponizsze wartosci:
    hh = 10; // godzina startu
    mm = 30; // minuty startu
    srh = 6; // godzina wschodu slonca
    srm = 0; // minuty wschodu slonca
    ssh = 20; // godzina zachodu slonca
    ssm = 0; // minuty zachodu slonca
    TGroundNode *LastNode = NULL; // do u�ycia w trainset
    iNumNodes = 0;
    token = "";
    parser.getTokens();
    parser >> token;
    int refresh = 0;

    while (token != "") //(!Parser->EndOfFile)
    {
        if (refresh == 50)
        { // SwapBuffers(hDC); //Ra: bez ogranicznika za bardzo spowalnia :( a u niekt�rych miga
            refresh = 0;
            Global::DoEvents();
        }
        else
            ++refresh;
        str = AnsiString(token.c_str());
        if (str == AnsiString("node"))
        {
            LastNode = AddGroundNode(&parser); // rozpoznanie w�z�a
            if (LastNode)
            { // je�eli przetworzony poprawnie
                if (LastNode->iType == GL_TRIANGLES)
                {
                    if (!LastNode->Vertices)
                        SafeDelete(LastNode); // usuwamy nieprzezroczyste tr�jk�ty terenu
                }
                else if (Global::bLoadTraction ? false : LastNode->iType == TP_TRACTION)
                    SafeDelete(LastNode); // usuwamy druty, je�li wy��czone
                if (LastNode) // dopiero na koniec dopisujemy do tablic
                    if (LastNode->iType != TP_DYNAMIC)
                    { // je�li nie jest pojazdem
                        LastNode->nNext = nRootOfType[LastNode->iType]; // ostatni dodany do��czamy
                        // na ko�cu nowego
                        nRootOfType[LastNode->iType] =
                            LastNode; // ustawienie nowego na pocz�tku listy
                        iNumNodes++;
                    }
                    else
                    { // je�li jest pojazdem
                        // if (!bInitDone) FirstInit(); //je�li nie by�o w scenerii
                        if (LastNode->DynamicObject->Mechanik) // ale mo�e by� pasa�er
                            if (LastNode->DynamicObject->Mechanik
                                    ->Primary()) // je�li jest g��wnym (pasa�er nie jest)
                                nTrainSetDriver =
                                    LastNode; // pojazd, kt�remu zostanie wys�any rozk�ad
                        LastNode->nNext = nRootDynamic;
                        nRootDynamic = LastNode; // dopisanie z przodu do listy
                        // if (bTrainSet && (LastNode?(LastNode->iType==TP_DYNAMIC):false))
                        if (nTrainSetNode) // je�eli istnieje wcze�niejszy TP_DYNAMIC
                            nTrainSetNode->DynamicObject->AttachPrev(
                                LastNode->DynamicObject,
                                TempConnectionType[iTrainSetWehicleNumber - 2]);
                        nTrainSetNode = LastNode; // ostatnio wczytany
                        if (TempConnectionType[iTrainSetWehicleNumber - 1] ==
                            0) // je�li sprz�g jest zerowy, to wys�a� rozk�ad do sk�adu
                        { // powinien te� tu wchodzi�, gdy pojazd bez trainset
                            if (nTrainSetDriver) // pojazd, kt�remu zostanie wys�any rozk�ad
                            { // wys�anie komendy "Timetable" ustawia odpowiedni tryb jazdy
                                nTrainSetDriver->DynamicObject->Mechanik->DirectionInitial();
                                nTrainSetDriver->DynamicObject->Mechanik->PutCommand(
                                    "Timetable:" + asTrainName, fTrainSetVel, 0, NULL);
                                nTrainSetDriver =
                                    NULL; // a przy "endtrainset" ju� wtedy nie potrzeba
                            }
                        }
                    }
            }
            else
            {
                Error("Scene parse error near " + AnsiString(token.c_str()));
                // break;
            }
        }
        else if (str == AnsiString("trainset"))
        {
            iTrainSetWehicleNumber = 0;
            nTrainSetNode = NULL;
            nTrainSetDriver = NULL; // pojazd, kt�remu zostanie wys�any rozk�ad
            bTrainSet = true;
            parser.getTokens();
            parser >> token;
            asTrainName = AnsiString(token.c_str()); // McZapkie: rodzaj+nazwa pociagu w SRJP
            parser.getTokens();
            parser >> token;
            asTrainSetTrack = AnsiString(token.c_str()); //�cie�ka startowa
            parser.getTokens(2);
            parser >> fTrainSetDist >> fTrainSetVel; // przesuni�cie i pr�dko��
        }
        else if (str == AnsiString("endtrainset"))
        { // McZapkie-110103: sygnaly konca pociagu ale tylko dla pociagow rozkladowych
            if (nTrainSetNode) // trainset bez dynamic si� sypa�
            { // powinien te� tu wchodzi�, gdy pojazd bez trainset
                if (nTrainSetDriver) // pojazd, kt�remu zostanie wys�any rozk�ad
                { // wys�anie komendy "Timetable" ustawia odpowiedni tryb jazdy
                    nTrainSetDriver->DynamicObject->Mechanik->DirectionInitial();
                    nTrainSetDriver->DynamicObject->Mechanik->PutCommand("Timetable:" + asTrainName,
                                                                         fTrainSetVel, 0, NULL);
                }
            }
            if (LastNode) // ostatni wczytany obiekt
                if (LastNode->iType ==
                    TP_DYNAMIC) // o ile jest pojazdem (na og� jest, ale kto wie...)
                    if (iTrainSetWehicleNumber ? !TempConnectionType[iTrainSetWehicleNumber - 1] :
                                                 false) // je�li ostatni pojazd ma sprz�g 0
                        LastNode->DynamicObject->RaLightsSet(-1, 2 + 32 + 64); // to za�o�ymy mu
            // ko�c�wki blaszane
            // (jak AI si�
            // odpali, to sobie
            // poprawi)
            bTrainSet = false;
            fTrainSetVel = 0;
            // iTrainSetConnection=0;
            nTrainSetNode = nTrainSetDriver = NULL;
            iTrainSetWehicleNumber = 0;
        }
        else if (str == AnsiString("event"))
        {
            TEvent *tmp = new TEvent();
            tmp->Load(&parser, &pOrigin);
            if (tmp->Type == tp_Unknown)
                delete tmp;
            else
            { // najpierw sprawdzamy, czy nie ma, a potem dopisujemy
                TEvent *found = FindEvent(tmp->asName);
                if (found)
                { // je�li znaleziony duplikat
                    int i = tmp->asName.Length();
                    if (tmp->asName[1] == '#') // zawsze jeden znak co najmniej jest
                    {
                        delete tmp;
                        tmp = NULL;
                    } // utylizacja duplikatu z krzy�ykiem
                    else if (i > 8 ? tmp->asName.SubString(1, 9) == "lineinfo:" :
                                     false) // tymczasowo wyj�tki
                    {
                        delete tmp;
                        tmp = NULL;
                    } // tymczasowa utylizacja duplikat�w W5
                    else if (i > 8 ? tmp->asName.SubString(i - 7, 8) == "_warning" :
                                     false) // tymczasowo wyj�tki
                    {
                        delete tmp;
                        tmp = NULL;
                    } // tymczasowa utylizacja duplikatu z tr�bieniem
                    else if (i > 4 ? tmp->asName.SubString(i - 3, 4) == "_shp" :
                                     false) // nie podlegaj� logowaniu
                    {
                        delete tmp;
                        tmp = NULL;
                    } // tymczasowa utylizacja duplikatu SHP
                    if (tmp) // je�li nie zosta� zutylizowany
                        if (Global::bJoinEvents)
                            found->Append(tmp); // doczepka (taki wirtualny multiple bez warunk�w)
                        else
                        {
                            ErrorLog("Duplicated event: " + tmp->asName);
                            found->Append(tmp); // doczepka (taki wirtualny multiple bez warunk�w)
                            found->Type = tp_Ignored; // dezaktywacja pierwotnego - taka proteza na
                            // wsteczn� zgodno��
                            // SafeDelete(tmp); //bezlito�nie usuwamy wszelkie duplikaty, �eby nie
                            // za�mieca� drzewka
                        }
                }
                if (tmp)
                { // je�li nie duplikat
                    tmp->evNext2 = RootEvent; // lista wszystkich event�w (m.in. do InitEvents)
                    RootEvent = tmp;
                    if (!found)
                    { // je�li nazwa wyst�pi�a, to do kolejki i wyszukiwarki dodawany jest tylko
                        // pierwszy
                        if (RootEvent->Type != tp_Ignored)
                            if (RootEvent->asName.Pos(
                                    "onstart")) // event uruchamiany automatycznie po starcie
                                AddToQuery(RootEvent, NULL); // dodanie do kolejki
                        sTracks->Add(0, tmp->asName.c_str(), tmp); // dodanie do wyszukiwarki
                    }
                }
            }
        }
        //     else
        //     if (str==AnsiString("include"))  //Tolaris to zrobil wewnatrz parsera
        //     {
        //         Include(Parser);
        //     }
        else if (str == AnsiString("rotate"))
        {
            // parser.getTokens(3);
            // parser >> aRotate.x >> aRotate.y >> aRotate.z; //Ra: to potrafi dawa� b��dne
            // rezultaty
            parser.getTokens();
            parser >> aRotate.x;
            parser.getTokens();
            parser >> aRotate.y;
            parser.getTokens();
            parser >> aRotate.z;
            // WriteLog("*** rotate "+AnsiString(aRotate.x)+" "+AnsiString(aRotate.y)+"
            // "+AnsiString(aRotate.z));
        }
        else if (str == AnsiString("origin"))
        {
            //      str=Parser->GetNextSymbol().LowerCase();
            //      if (str=="begin")
            {
                if (OriginStackTop >= OriginStackMaxDepth - 1)
                {
                    MessageBox(0, AnsiString("Origin stack overflow ").c_str(), "Error", MB_OK);
                    break;
                }
                parser.getTokens(3);
                parser >> OriginStack[OriginStackTop].x >> OriginStack[OriginStackTop].y >>
                    OriginStack[OriginStackTop].z;
                pOrigin += OriginStack[OriginStackTop]; // sumowanie ca�kowitego przesuni�cia
                OriginStackTop++; // zwi�kszenie wska�nika stosu
            }
        }
        else if (str == AnsiString("endorigin"))
        {
            //      else
            //    if (str=="end")
            {
                if (OriginStackTop <= 0)
                {
                    MessageBox(0, AnsiString("Origin stack underflow ").c_str(), "Error", MB_OK);
                    break;
                }

                OriginStackTop--; // zmniejszenie wska�nika stosu
                pOrigin -= OriginStack[OriginStackTop];
            }
        }
        else if (str == AnsiString("atmo")) // TODO: uporzadkowac gdzie maja byc parametry mgly!
        { // Ra: ustawienie parametr�w OpenGL przeniesione do FirstInit
            WriteLog("Scenery atmo definition");
            parser.getTokens(3);
            parser >> Global::AtmoColor[0] >> Global::AtmoColor[1] >> Global::AtmoColor[2];
            parser.getTokens(2);
            parser >> Global::fFogStart >> Global::fFogEnd;
            if (Global::fFogEnd > 0.0)
            { // ostatnie 3 parametry s� opcjonalne
                parser.getTokens(3);
                parser >> Global::FogColor[0] >> Global::FogColor[1] >> Global::FogColor[2];
            }
            parser.getTokens();
            parser >> token;
            while (token.compare("endatmo") != 0)
            { // a kolejne parametry s� pomijane
                parser.getTokens();
                parser >> token;
            }
        }
        else if (str == AnsiString("time"))
        {
            WriteLog("Scenery time definition");
            char temp_in[9];
            char temp_out[9];
            int i, j;
            parser.getTokens();
            parser >> temp_in;
            for (j = 0; j <= 8; j++)
                temp_out[j] = ' ';
            for (i = 0; temp_in[i] != ':'; i++)
                temp_out[i] = temp_in[i];
            hh = atoi(temp_out);
            for (j = 0; j <= 8; j++)
                temp_out[j] = ' ';
            for (j = i + 1; j <= 8; j++)
                temp_out[j - (i + 1)] = temp_in[j];
            mm = atoi(temp_out);

            parser.getTokens();
            parser >> temp_in;
            for (j = 0; j <= 8; j++)
                temp_out[j] = ' ';
            for (i = 0; temp_in[i] != ':'; i++)
                temp_out[i] = temp_in[i];
            srh = atoi(temp_out);
            for (j = 0; j <= 8; j++)
                temp_out[j] = ' ';
            for (j = i + 1; j <= 8; j++)
                temp_out[j - (i + 1)] = temp_in[j];
            srm = atoi(temp_out);

            parser.getTokens();
            parser >> temp_in;
            for (j = 0; j <= 8; j++)
                temp_out[j] = ' ';
            for (i = 0; temp_in[i] != ':'; i++)
                temp_out[i] = temp_in[i];
            ssh = atoi(temp_out);
            for (j = 0; j <= 8; j++)
                temp_out[j] = ' ';
            for (j = i + 1; j <= 8; j++)
                temp_out[j - (i + 1)] = temp_in[j];
            ssm = atoi(temp_out);
            while (token.compare("endtime") != 0)
            {
                parser.getTokens();
                parser >> token;
            }
        }
        else if (str == AnsiString("light"))
        { // Ra: ustawianie �wiat�a przeniesione do FirstInit
            WriteLog("Scenery light definition");
            vector3 lp;
            parser.getTokens();
            parser >> lp.x;
            parser.getTokens();
            parser >> lp.y;
            parser.getTokens();
            parser >> lp.z;
            lp = Normalize(lp); // kierunek padania
            Global::lightPos[0] = lp.x; // daylight position
            Global::lightPos[1] = lp.y;
            Global::lightPos[2] = lp.z;
            parser.getTokens();
            parser >> Global::ambientDayLight[0]; // kolor wszechobceny
            parser.getTokens();
            parser >> Global::ambientDayLight[1];
            parser.getTokens();
            parser >> Global::ambientDayLight[2];

            parser.getTokens();
            parser >> Global::diffuseDayLight[0]; // kolor padaj�cy
            parser.getTokens();
            parser >> Global::diffuseDayLight[1];
            parser.getTokens();
            parser >> Global::diffuseDayLight[2];

            parser.getTokens();
            parser >> Global::specularDayLight[0]; // kolor odbity
            parser.getTokens();
            parser >> Global::specularDayLight[1];
            parser.getTokens();
            parser >> Global::specularDayLight[2];

            do
            {
                parser.getTokens();
                parser >> token;
            } while (token.compare("endlight") != 0);
        }
        else if (str == AnsiString("camera"))
        {
            vector3 xyz, abc;
            xyz = abc = vector3(0, 0, 0); // warto�ci domy�lne, bo nie wszystie musz� by�
            int i = -1, into = -1; // do kt�rej definicji kamery wstawi�
            WriteLog("Scenery camera definition");
            do
            { // opcjonalna si�dma liczba okre�la numer kamery, a kiedy� by�y tylko 3
                parser.getTokens();
                parser >> token;
                switch (++i)
                { // kiedy� camera mia�o tylko 3 wsp�rz�dne
                case 0:
                    xyz.x = atof(token.c_str());
                    break;
                case 1:
                    xyz.y = atof(token.c_str());
                    break;
                case 2:
                    xyz.z = atof(token.c_str());
                    break;
                case 3:
                    abc.x = atof(token.c_str());
                    break;
                case 4:
                    abc.y = atof(token.c_str());
                    break;
                case 5:
                    abc.z = atof(token.c_str());
                    break;
                case 6:
                    into = atoi(token.c_str()); // takie sobie, bo mo�na wpisa� -1
                }
            } while (token.compare("endcamera") != 0);
            if (into < 0)
                into = ++Global::iCameraLast;
            if ((into >= 0) && (into < 10))
            { // przepisanie do odpowiedniego miejsca w tabelce
                Global::pFreeCameraInit[into] = xyz;
                abc.x = DegToRad(abc.x);
                abc.y = DegToRad(abc.y);
                abc.z = DegToRad(abc.z);
                Global::pFreeCameraInitAngle[into] = abc;
                Global::iCameraLast = into; // numer ostatniej
            }
        }
        else if (str == AnsiString("sky"))
        { // youBy - niebo z pliku
            WriteLog("Scenery sky definition");
            parser.getTokens();
            parser >> token;
            AnsiString SkyTemp;
            SkyTemp = AnsiString(token.c_str());
            if (Global::asSky == "1")
                Global::asSky = SkyTemp;
            do
            { // po�arcie dodatkowych parametr�w
                parser.getTokens();
                parser >> token;
            } while (token.compare("endsky") != 0);
            WriteLog(Global::asSky.c_str());
        }
        else if (str == AnsiString("firstinit"))
            FirstInit();
        else if (str == AnsiString("description"))
        {
            do
            {
                parser.getTokens();
                parser >> token;
            } while (token.compare("enddescription") != 0);
        }
        else if (str == AnsiString("test"))
        { // wypisywanie tre�ci po przetworzeniu
            WriteLog("---> Parser test:");
            do
            {
                parser.getTokens();
                parser >> token;
                WriteLog(token.c_str());
            } while (token.compare("endtest") != 0);
            WriteLog("---> End of parser test.");
        }
        else if (str == AnsiString("config"))
        { // mo�liwo�� przedefiniowania parametr�w w scenerii
            Global::ConfigParse(NULL, &parser); // parsowanie dodatkowych ustawie�
        }
        else if (str != AnsiString(""))
        { // pomijanie od nierozpoznanej komendy do jej zako�czenia
            if ((token.length() > 2) && (atof(token.c_str()) == 0.0))
            { // je�li nie liczba, to spr�bowa� pomin�� komend�
                WriteLog(AnsiString("Unrecognized command: " + str));
                str = "end" + str;
                do
                {
                    parser.getTokens();
                    token = "";
                    parser >> token;
                } while ((token != "") && (token.compare(str.c_str()) != 0));
            }
            else // jak liczba to na pewno b��d
                Error(AnsiString("Unrecognized command: " + str));
        }
        else if (str == AnsiString(""))
            break;

        // LastNode=NULL;

        token = "";
        parser.getTokens();
        parser >> token;
    }

    delete parser;
    sTracks->Sort(TP_TRACK); // finalne sortowanie drzewa tor�w
    sTracks->Sort(TP_MEMCELL); // finalne sortowanie drzewa kom�rek pami�ci
    sTracks->Sort(TP_MODEL); // finalne sortowanie drzewa modeli
    sTracks->Sort(0); // finalne sortowanie drzewa event�w
    if (!bInitDone)
        FirstInit(); // je�li nie by�o w scenerii
    if (Global::pTerrainCompact)
        TerrainWrite(); // Ra: teraz mo�na zapisa� teren w jednym pliku
    Global::iPause &= ~0x10; // koniec pauzy wczytywania
    return true;
}

bool TGround::InitEvents()
{ //��czenie event�w z pozosta�ymi obiektami
    TGroundNode *tmp, *trk;
    char buff[255];
    int i;
    for (TEvent *Current = RootEvent; Current; Current = Current->evNext2)
    {
        switch (Current->Type)
        {
        case tp_AddValues: // sumowanie warto�ci
        case tp_UpdateValues: // zmiana warto�ci
            tmp = FindGroundNode(Current->asNodeName,
                                 TP_MEMCELL); // nazwa kom�rki powi�zanej z eventem
            if (tmp)
            { // McZapkie-100302
                if (Current->iFlags & (conditional_trackoccupied | conditional_trackfree))
                { // je�li chodzi o zajetosc toru (tor mo�e by� inny, ni� wpisany w kom�rce)
                    trk = FindGroundNode(Current->asNodeName,
                                         TP_TRACK); // nazwa toru ta sama, co nazwa kom�rki
                    if (trk)
                        Current->Params[9].asTrack = trk->pTrack;
                    if (!Current->Params[9].asTrack)
                        ErrorLog("Bad event: track \"" + AnsiString(Current->asNodeName) +
                                 "\" does not exists in \"" + Current->asName + "\"");
                }
                Current->Params[4].nGroundNode = tmp;
                Current->Params[5].asMemCell = tmp->MemCell; // kom�rka do aktualizacji
                if (Current->iFlags & (conditional_memcompare))
                    Current->Params[9].asMemCell = tmp->MemCell; // kom�rka do badania warunku
                if (!tmp->MemCell->asTrackName
                         .IsEmpty()) // tor powi�zany z kom�rk� powi�zan� z eventem
                { // tu potrzebujemy wska�nik do kom�rki w (tmp)
                    trk = FindGroundNode(tmp->MemCell->asTrackName, TP_TRACK);
                    if (trk)
                        Current->Params[6].asTrack = trk->pTrack;
                    else
                        ErrorLog("Bad memcell: track \"" + tmp->MemCell->asTrackName +
                                 "\" not exists in memcell \"" + tmp->asName + "\"");
                }
                else
                    Current->Params[6].asTrack = NULL;
            }
            else
            { // nie ma kom�rki, to nie b�dzie dzia�a� poprawnie
                Current->Type = tp_Ignored; // deaktywacja
                ErrorLog("Bad event: \"" + Current->asName + "\" cannot find memcell \"" +
                         Current->asNodeName + "\"");
            }
            break;
        case tp_LogValues: // skojarzenie z memcell
            if (Current->asNodeName.IsEmpty())
            { // brak skojarzenia daje logowanie wszystkich
                Current->Params[9].asMemCell = NULL;
                break;
            }
        case tp_GetValues:
        case tp_WhoIs:
            tmp = FindGroundNode(Current->asNodeName, TP_MEMCELL);
            if (tmp)
            {
                Current->Params[8].nGroundNode = tmp;
                Current->Params[9].asMemCell = tmp->MemCell;
                if (Current->Type == tp_GetValues) // je�li odczyt kom�rki
                    if (tmp->MemCell->IsVelocity()) // a kom�rka zawiera komend� SetVelocity albo
                        // ShuntVelocity
                        Current->bEnabled = false; // to event nie b�dzie dodawany do kolejki
            }
            else
            { // nie ma kom�rki, to nie b�dzie dzia�a� poprawnie
                Current->Type = tp_Ignored; // deaktywacja
                ErrorLog("Bad event: \"" + Current->asName + "\" cannot find memcell \"" +
                         Current->asNodeName + "\"");
            }
            break;
        case tp_CopyValues: // skopiowanie kom�rki do innej
            tmp = FindGroundNode(Current->asNodeName, TP_MEMCELL); // kom�rka docelowa
            if (tmp)
            {
                Current->Params[4].nGroundNode = tmp;
                Current->Params[5].asMemCell = tmp->MemCell; // kom�rka docelowa
                if (!tmp->MemCell->asTrackName
                         .IsEmpty()) // tor powi�zany z kom�rk� powi�zan� z eventem
                { // tu potrzebujemy wska�nik do kom�rki w (tmp)
                    trk = FindGroundNode(tmp->MemCell->asTrackName, TP_TRACK);
                    if (trk)
                        Current->Params[6].asTrack = trk->pTrack;
                    else
                        ErrorLog("Bad memcell: track \"" + tmp->MemCell->asTrackName +
                                 "\" not exists in memcell \"" + tmp->asName + "\"");
                }
                else
                    Current->Params[6].asTrack = NULL;
            }
            else
                ErrorLog("Bad copyvalues: event \"" + Current->asName +
                         "\" cannot find memcell \"" + Current->asNodeName + "\"");
            strcpy(
                buff,
                Current->Params[9].asText); // skopiowanie nazwy drugiej kom�rki do bufora roboczego
            SafeDeleteArray(Current->Params[9].asText); // usuni�cie nazwy kom�rki
            tmp = FindGroundNode(buff, TP_MEMCELL); // kom�rka ��d�owa
            if (tmp)
            {
                Current->Params[8].nGroundNode = tmp;
                Current->Params[9].asMemCell = tmp->MemCell; // kom�rka �r�d�owa
            }
            else
                ErrorLog("Bad copyvalues: event \"" + Current->asName +
                         "\" cannot find memcell \"" + AnsiString(buff) + "\"");
            break;
        case tp_Animation: // animacja modelu
            tmp = FindGroundNode(Current->asNodeName, TP_MODEL); // egzemplarza modelu do animowania
            if (tmp)
            {
                strcpy(
                    buff,
                    Current->Params[9].asText); // skopiowanie nazwy submodelu do bufora roboczego
                SafeDeleteArray(Current->Params[9].asText); // usuni�cie nazwy submodelu
                if (Current->Params[0].asInt == 4)
                    Current->Params[9].asModel = tmp->Model; // model dla ca�omodelowych animacji
                else
                { // standardowo przypisanie submodelu
                    Current->Params[9].asAnimContainer = tmp->Model->GetContainer(buff); // submodel
                    if (Current->Params[9].asAnimContainer)
                    {
                        Current->Params[9].asAnimContainer->WillBeAnimated(); // oflagowanie
                        // animacji
                        if (!Current->Params[9]
                                 .asAnimContainer->Event()) // nie szuka�, gdy znaleziony
                            Current->Params[9].asAnimContainer->EventAssign(
                                FindEvent(Current->asNodeName + "." + AnsiString(buff) + ":done"));
                    }
                }
            }
            else
                ErrorLog("Bad animation: event \"" + Current->asName + "\" cannot find model \"" +
                         Current->asNodeName + "\"");
            Current->asNodeName = "";
            break;
        case tp_Lights: // zmiana �wiete� modelu
            tmp = FindGroundNode(Current->asNodeName, TP_MODEL);
            if (tmp)
                Current->Params[9].asModel = tmp->Model;
            else
                ErrorLog("Bad lights: event \"" + Current->asName + "\" cannot find model \"" +
                         Current->asNodeName + "\"");
            Current->asNodeName = "";
            break;
        case tp_Visible: // ukrycie albo przywr�cenie obiektu
            tmp = FindGroundNode(Current->asNodeName, TP_MODEL); // najpierw model
            if (!tmp)
                tmp = FindGroundNode(Current->asNodeName, TP_TRACTION); // mo�e druty?
            if (!tmp)
                tmp = FindGroundNode(Current->asNodeName, TP_TRACK); // albo tory?
            if (tmp)
                Current->Params[9].nGroundNode = tmp;
            else
                ErrorLog("Bad visibility: event \"" + Current->asName + "\" cannot find model \"" +
                         Current->asNodeName + "\"");
            Current->asNodeName = "";
            break;
        case tp_Switch: // prze�o�enie zwrotnicy albo zmiana stanu obrotnicy
            tmp = FindGroundNode(Current->asNodeName, TP_TRACK);
            if (tmp)
            { // dowi�zanie toru
                if (!tmp->pTrack->iAction) // je�li nie jest zwrotnic� ani obrotnic�
                    tmp->pTrack->iAction |= 0x100; // to b�dzie si� zmienia� stan uszkodzenia
                Current->Params[9].asTrack = tmp->pTrack;
                if (!Current->Params[0].asInt) // je�li prze��cza do stanu 0
                    if (Current->Params[2].asdouble >=
                        0.0) // je�li jest zdefiniowany dodatkowy ruch iglic
                        Current->Params[9].asTrack->Switch(
                            0, Current->Params[1].asdouble,
                            Current->Params[2].asdouble); // przes�anie parametr�w
            }
            else
                ErrorLog("Bad switch: event \"" + Current->asName + "\" cannot find track \"" +
                         Current->asNodeName + "\"");
            Current->asNodeName = "";
            break;
        case tp_Sound: // odtworzenie d�wi�ku
            tmp = FindGroundNode(Current->asNodeName, TP_SOUND);
            if (tmp)
                Current->Params[9].tsTextSound = tmp->tsStaticSound;
            else
                ErrorLog("Bad sound: event \"" + Current->asName +
                         "\" cannot find static sound \"" + Current->asNodeName + "\"");
            Current->asNodeName = "";
            break;
        case tp_TrackVel: // ustawienie pr�dko�ci na torze
            if (!Current->asNodeName.IsEmpty())
            {
                tmp = FindGroundNode(Current->asNodeName, TP_TRACK);
                if (tmp)
                {
                    tmp->pTrack->iAction |=
                        0x200; // flaga zmiany pr�dko�ci toru jest istotna dla skanowania
                    Current->Params[9].asTrack = tmp->pTrack;
                }
                else
                    ErrorLog("Bad velocity: event \"" + Current->asName +
                             "\" cannot find track \"" + Current->asNodeName + "\"");
            }
            Current->asNodeName = "";
            break;
        case tp_DynVel: // komunikacja z pojazdem o konkretnej nazwie
            if (Current->asNodeName == "activator")
                Current->Params[9].asDynamic = NULL;
            else
            {
                tmp = FindGroundNode(Current->asNodeName, TP_DYNAMIC);
                if (tmp)
                    Current->Params[9].asDynamic = tmp->DynamicObject;
                else
                    Error("Event \"" + Current->asName + "\" cannot find dynamic \"" +
                          Current->asNodeName + "\"");
            }
            Current->asNodeName = "";
            break;
        case tp_Multiple:
            if (Current->Params[9].asText != NULL)
            { // przepisanie nazwy do bufora
                strcpy(buff, Current->Params[9].asText);
                SafeDeleteArray(Current->Params[9].asText);
                Current->Params[9].asPointer = NULL; // zerowanie wska�nika, aby wykry� brak obeiktu
            }
            else
                buff[0] = '\0';
            if (Current->iFlags & (conditional_trackoccupied | conditional_trackfree))
            { // je�li chodzi o zajetosc toru
                tmp = FindGroundNode(buff, TP_TRACK);
                if (tmp)
                    Current->Params[9].asTrack = tmp->pTrack;
                if (!Current->Params[9].asTrack)
                {
                    ErrorLog(AnsiString("Bad event: Track \"") + AnsiString(buff) +
                             "\" does not exist in \"" + Current->asName + "\"");
                    Current->iFlags &=
                        ~(conditional_trackoccupied | conditional_trackfree); // zerowanie flag
                }
            }
            else if (Current->iFlags &
                     (conditional_memstring | conditional_memval1 | conditional_memval2))
            { // je�li chodzi o komorke pamieciow�
                tmp = FindGroundNode(buff, TP_MEMCELL);
                if (tmp)
                    Current->Params[9].asMemCell = tmp->MemCell;
                if (!Current->Params[9].asMemCell)
                {
                    ErrorLog(AnsiString("Bad event: MemCell \"") + AnsiString(buff) +
                             AnsiString("\" does not exist in \"" + Current->asName + "\""));
                    Current->iFlags &=
                        ~(conditional_memstring | conditional_memval1 | conditional_memval2);
                }
            }
            for (i = 0; i < 8; i++)
            {
                if (Current->Params[i].asText != NULL)
                {
                    strcpy(buff, Current->Params[i].asText);
                    SafeDeleteArray(Current->Params[i].asText);
                    Current->Params[i].asEvent = FindEvent(buff);
                    if (!Current->Params[i].asEvent) // Ra: tylko w logu informacja o braku
                        if (AnsiString(Current->Params[i].asText).SubString(1, 5) != "none_")
                        {
                            WriteLog(AnsiString("Event \"") + AnsiString(buff) +
                                     AnsiString("\" does not exist"));
                            ErrorLog("Missed event: " + AnsiString(buff) + " in multiple " +
                                     Current->asName);
                        }
                }
            }
            break;
        case tp_Voltage: // zmiana napi�cia w zasilaczu (TractionPowerSource)
            if (!Current->asNodeName.IsEmpty())
            {
                tmp = FindGroundNode(Current->asNodeName,
                                     TP_TRACTIONPOWERSOURCE); // pod��czenie zasilacza
                if (tmp)
                    Current->Params[9].psPower = tmp->psTractionPowerSource;
                else
                    ErrorLog("Bad voltage: event \"" + Current->asName +
                             "\" cannot find power source \"" + Current->asNodeName + "\"");
            }
            Current->asNodeName = "";
            break;
        case tp_Message: // wy�wietlenie komunikatu
            break;
        }
        if (Current->fDelay < 0)
            AddToQuery(Current, NULL);
    }
    for (TGroundNode *Current = nRootOfType[TP_MEMCELL]; Current; Current = Current->nNext)
    { // Ra: eventy kom�rek pami�ci, wykonywane po wys�aniu komendy do zatrzymanego pojazdu
        Current->MemCell->AssignEvents(FindEvent(Current->asName + ":sent"));
    }
    return true;
}

void TGround::InitTracks()
{ //��czenie tor�w ze sob� i z eventami
    TGroundNode *Current, *Model;
    TTrack *tmp; // znaleziony tor
    TTrack *Track;
    int iConnection, state;
    AnsiString name;
    // tracks=tracksfar=0;
    for (Current = nRootOfType[TP_TRACK]; Current; Current = Current->nNext)
    {
        Track = Current->pTrack;
        if (Global::iHiddenEvents & 1)
            if (!Current->asName.IsEmpty())
            { // je�li podana jest nazwa tor�w, mo�na szuka� event�w skojarzonych przez nazw�
                if (Track->asEvent0Name.IsEmpty())
                    if (FindEvent(Current->asName + ":event0"))
                        Track->asEvent0Name = Current->asName + ":event0";
                if (Track->asEvent1Name.IsEmpty())
                    if (FindEvent(Current->asName + ":event1"))
                        Track->asEvent1Name = Current->asName + ":event1";
                if (Track->asEvent2Name.IsEmpty())
                    if (FindEvent(Current->asName + ":event2"))
                        Track->asEvent2Name = Current->asName + ":event2";

                if (Track->asEventall0Name.IsEmpty())
                    if (FindEvent(Current->asName + ":eventall0"))
                        Track->asEventall0Name = Current->asName + ":eventall0";
                if (Track->asEventall1Name.IsEmpty())
                    if (FindEvent(Current->asName + ":eventall1"))
                        Track->asEventall1Name = Current->asName + ":eventall1";
                if (Track->asEventall2Name.IsEmpty())
                    if (FindEvent(Current->asName + ":eventall2"))
                        Track->asEventall2Name = Current->asName + ":eventall2";
            }
        Track->AssignEvents(
            Track->asEvent0Name.IsEmpty() ? NULL : FindEvent(Track->asEvent0Name),
            Track->asEvent1Name.IsEmpty() ? NULL : FindEventScan(Track->asEvent1Name),
            Track->asEvent2Name.IsEmpty() ? NULL : FindEventScan(Track->asEvent2Name));
        Track->AssignallEvents(
            Track->asEventall0Name.IsEmpty() ? NULL : FindEvent(Track->asEventall0Name),
            Track->asEventall1Name.IsEmpty() ? NULL : FindEvent(Track->asEventall1Name),
            Track->asEventall2Name.IsEmpty() ? NULL :
                                               FindEvent(Track->asEventall2Name)); // MC-280503
        switch (Track->eType)
        {
        case tt_Table: // obrotnic� te� ��czymy na starcie z innymi torami
            Model = FindGroundNode(Current->asName, TP_MODEL); // szukamy modelu o tej samej nazwie
            // if (tmp) //mamy model, trzeba zapami�ta� wska�nik do jego animacji
            { // jak co� p�jdzie �le, to robimy z tego normalny tor
                // Track->ModelAssign(tmp->Model->GetContainer(NULL)); //wi�zanie toru z modelem
                // obrotnicy
                Track->RaAssign(
                    Current, Model ? Model->Model : NULL, FindEvent(Current->asName + ":done"),
                    FindEvent(Current->asName + ":joined")); // wi�zanie toru z modelem obrotnicy
                // break; //jednak po��cz� z s�siednim, jak ma si� wysypywa� null track
            }
            if (!Model) // jak nie ma modelu
                break; // to pewnie jest wykolejnica, a ta jest domy�lnie zamkni�ta i wykoleja
        case tt_Normal: // tylko proste s� pod��czane do rozjazd�w, st�d dwa rozjazdy si� nie
            // po��cz� ze sob�
            if (Track->CurrentPrev() == NULL) // tylko je�li jeszcze nie pod��czony
            {
                tmp = FindTrack(Track->CurrentSegment()->FastGetPoint_0(), iConnection, Current);
                switch (iConnection)
                {
                case -1: // Ra: pierwsza koncepcja zawijania samochod�w i statk�w
                    // if ((Track->iCategoryFlag&1)==0) //je�li nie jest torem szynowym
                    // Track->ConnectPrevPrev(Track,0); //��czenie ko�ca odcinka do samego siebie
                    break;
                case 0:
                    Track->ConnectPrevPrev(tmp, 0);
                    break;
                case 1:
                    Track->ConnectPrevNext(tmp, 1);
                    break;
                case 2:
                    Track->ConnectPrevPrev(tmp, 0); // do Point1 pierwszego
                    tmp->SetConnections(0); // zapami�tanie ustawie� w Segmencie
                    break;
                case 3:
                    Track->ConnectPrevNext(tmp, 1); // do Point2 pierwszego
                    tmp->SetConnections(0); // zapami�tanie ustawie� w Segmencie
                    break;
                case 4:
                    tmp->Switch(1);
                    Track->ConnectPrevPrev(tmp, 2); // do Point1 drugiego
                    tmp->SetConnections(1); // robi te� Switch(0)
                    tmp->Switch(0);
                    break;
                case 5:
                    tmp->Switch(1);
                    Track->ConnectPrevNext(tmp, 3); // do Point2 drugiego
                    tmp->SetConnections(1); // robi te� Switch(0)
                    tmp->Switch(0);
                    break;
                }
            }
            if (Track->CurrentNext() == NULL) // tylko je�li jeszcze nie pod��czony
            {
                tmp = FindTrack(Track->CurrentSegment()->FastGetPoint_1(), iConnection, Current);
                switch (iConnection)
                {
                case -1: // Ra: pierwsza koncepcja zawijania samochod�w i statk�w
                    // if ((Track->iCategoryFlag&1)==0) //je�li nie jest torem szynowym
                    // Track->ConnectNextNext(Track,1); //��czenie ko�ca odcinka do samego siebie
                    break;
                case 0:
                    Track->ConnectNextPrev(tmp, 0);
                    break;
                case 1:
                    Track->ConnectNextNext(tmp, 1);
                    break;
                case 2:
                    Track->ConnectNextPrev(tmp, 0);
                    tmp->SetConnections(0); // zapami�tanie ustawie� w Segmencie
                    break;
                case 3:
                    Track->ConnectNextNext(tmp, 1);
                    tmp->SetConnections(0); // zapami�tanie ustawie� w Segmencie
                    break;
                case 4:
                    tmp->Switch(1);
                    Track->ConnectNextPrev(tmp, 2);
                    tmp->SetConnections(1); // robi te� Switch(0)
                    // tmp->Switch(0);
                    break;
                case 5:
                    tmp->Switch(1);
                    Track->ConnectNextNext(tmp, 3);
                    tmp->SetConnections(1); // robi te� Switch(0)
                    // tmp->Switch(0);
                    break;
                }
            }
            break;
        case tt_Switch: // dla rozjazd�w szukamy event�w sygnalizacji rozprucia
            Track->AssignForcedEvents(FindEvent(Current->asName + ":forced+"),
                                      FindEvent(Current->asName + ":forced-"));
            break;
        }
        name = Track->IsolatedName(); // pobranie nazwy odcinka izolowanego
        if (!name.IsEmpty()) // je�li zosta�a zwr�cona nazwa
            Track->IsolatedEventsAssign(FindEvent(name + ":busy"), FindEvent(name + ":free"));
        if (Current->asName.SubString(1, 1) ==
            "*") // mo�liwy portal, je�li nie pod��czony od striny 1
            if (!Track->CurrentPrev() && Track->CurrentNext())
                Track->iCategoryFlag |= 0x100; // ustawienie flagi portalu
    }
    // WriteLog("Total "+AnsiString(tracks)+", far "+AnsiString(tracksfar));
    TIsolated *p = TIsolated::Root();
    while (p)
    { // je�li si� znajdzie, to poda� wska�nik
        Current = FindGroundNode(p->asName, TP_MEMCELL); // czy jest kom�ka o odpowiedniej nazwie
        if (Current)
            p->pMemCell = Current->MemCell; // przypisanie powi�zanej kom�rki
        else
        { // utworzenie automatycznej kom�rki
            Current = new TGroundNode(); // to nie musi mie� nazwy, nazwa w wyszukiwarce wystarczy
            // Current->asName=p->asName; //mazwa identyczna, jak nazwa odcinka izolowanego
            Current->MemCell = new TMemCell(NULL); // nowa kom�rka
            sTracks->Add(TP_MEMCELL, p->asName.c_str(), Current); // dodanie do wyszukiwarki
            Current->nNext =
                nRootOfType[TP_MEMCELL]; // to nie powinno tutaj by�, bo robi si� �mietnik
            nRootOfType[TP_MEMCELL] = Current;
            iNumNodes++;
            p->pMemCell = Current->MemCell; // wska�nik kom�ki przekazany do odcinka izolowanego
        }
        p = p->Next();
    }
    // for (Current=nRootOfType[TP_TRACK];Current;Current=Current->nNext)
    // if (Current->pTrack->eType==tt_Cross)
    //  Current->pTrack->ConnectionsLog(); //zalogowanie informacji o po��czeniach
}

void TGround::InitTraction()
{ //��czenie drut�w ze sob� oraz z torami i eventami
    TGroundNode *nCurrent, *nTemp;
    TTraction *tmp; // znalezione prz�s�o
    TTraction *Traction;
    int iConnection;
    AnsiString name;
    for (nCurrent = nRootOfType[TP_TRACTION]; nCurrent; nCurrent = nCurrent->nNext)
    { // pod��czenie do zasilacza, �eby mo�na by�o sumowa� pr�d kilku pojazd�w
        // a jednocze�nie z jednego miejsca zmienia� napi�cie eventem
        // wykonywane najpierw, �eby mo�na by�o logowa� pod��czenie 2 zasilaczy do jednego drutu
        // izolator zawieszony na prz�le jest ma by� osobnym odcinkiem drutu o d�ugo�ci ok. 1m,
        // pod��czonym do zasilacza o nazwie "*" (gwiazka); "none" nie b�dzie odpowiednie
        Traction = nCurrent->hvTraction;
        nTemp = FindGroundNode(Traction->asPowerSupplyName, TP_TRACTIONPOWERSOURCE);
        if (nTemp) // jak zasilacz znaleziony
            Traction->PowerSet(nTemp->psTractionPowerSource); // to pod��czy� do prz�s�a
        else if (Traction->asPowerSupplyName != "*") // gwiazdka dla prz�s�a z izolatorem
            if (Traction->asPowerSupplyName != "none") // dopuszczamy na razie brak pod��czenia?
            { // logowanie b��du i utworzenie zasilacza o domy�lnej zawarto�ci
                ErrorLog("Missed TractionPowerSource: " + Traction->asPowerSupplyName);
                nTemp = new TGroundNode();
                nTemp->iType = TP_TRACTIONPOWERSOURCE;
                nTemp->asName = Traction->asPowerSupplyName;
                nTemp->psTractionPowerSource = new TTractionPowerSource(nTemp);
                nTemp->psTractionPowerSource->Init(Traction->NominalVoltage, Traction->MaxCurrent);
                nTemp->nNext = nRootOfType[nTemp->iType]; // ostatni dodany do��czamy na ko�cu
                // nowego
                nRootOfType[nTemp->iType] = nTemp; // ustawienie nowego na pocz�tku listy
                iNumNodes++;
            }
    }
    for (nCurrent = nRootOfType[TP_TRACTION]; nCurrent; nCurrent = nCurrent->nNext)
    {
        Traction = nCurrent->hvTraction;
        if (!Traction->hvNext[0]) // tylko je�li jeszcze nie pod��czony
        {
            tmp = FindTraction(&Traction->pPoint1, iConnection, nCurrent);
            switch (iConnection)
            {
            case 0:
                Traction->Connect(0, tmp, 0);
                break;
            case 1:
                Traction->Connect(0, tmp, 1);
                break;
            }
            if (Traction->hvNext[0]) // je�li zosta� pod��czony
                if (Traction->psSection && tmp->psSection) // tylko prz�s�o z izolatorem mo�e nie
                    // mie� zasilania, bo ma 2, trzeba
                    // sprawdza� s�siednie
                    if (Traction->psSection !=
                        tmp->psSection) // po��czone odcinki maj� r�ne zasilacze
                    { // to mo�e by� albo pod��czenie podstacji lub kabiny sekcyjnej do sekcji, albo
                        // b��d
                        if (Traction->psSection->bSection && !tmp->psSection->bSection)
                        { //(tmp->psSection) jest podstacj�, a (Traction->psSection) nazw� sekcji
                            tmp->PowerSet(Traction->psSection); // zast�pienie wskazaniem sekcji
                        }
                        else if (!Traction->psSection->bSection && tmp->psSection->bSection)
                        { //(Traction->psSection) jest podstacj�, a (tmp->psSection) nazw� sekcji
                            Traction->PowerSet(tmp->psSection); // zast�pienie wskazaniem sekcji
                        }
                        else // je�li obie to sekcje albo obie podstacje, to b�dzie b��d
                            ErrorLog("Bad power: at " +
                                     FloatToStrF(Traction->pPoint1.x, ffFixed, 6, 2) + " " +
                                     FloatToStrF(Traction->pPoint1.y, ffFixed, 6, 2) + " " +
                                     FloatToStrF(Traction->pPoint1.z, ffFixed, 6, 2));
                    }
        }
        if (!Traction->hvNext[1]) // tylko je�li jeszcze nie pod��czony
        {
            tmp = FindTraction(&Traction->pPoint2, iConnection, nCurrent);
            switch (iConnection)
            {
            case 0:
                Traction->Connect(1, tmp, 0);
                break;
            case 1:
                Traction->Connect(1, tmp, 1);
                break;
            }
            if (Traction->hvNext[1]) // je�li zosta� pod��czony
                if (Traction->psSection && tmp->psSection) // tylko prz�s�o z izolatorem mo�e nie
                    // mie� zasilania, bo ma 2, trzeba
                    // sprawdza� s�siednie
                    if (Traction->psSection != tmp->psSection)
                    { // to mo�e by� albo pod��czenie podstacji lub kabiny sekcyjnej do sekcji, albo
                        // b��d
                        if (Traction->psSection->bSection && !tmp->psSection->bSection)
                        { //(tmp->psSection) jest podstacj�, a (Traction->psSection) nazw� sekcji
                            tmp->PowerSet(Traction->psSection); // zast�pienie wskazaniem sekcji
                        }
                        else if (!Traction->psSection->bSection && tmp->psSection->bSection)
                        { //(Traction->psSection) jest podstacj�, a (tmp->psSection) nazw� sekcji
                            Traction->PowerSet(tmp->psSection); // zast�pienie wskazaniem sekcji
                        }
                        else // je�li obie to sekcje albo obie podstacje, to b�dzie b��d
                            ErrorLog("Bad power: at " +
                                     FloatToStrF(Traction->pPoint2.x, ffFixed, 6, 2) + " " +
                                     FloatToStrF(Traction->pPoint2.y, ffFixed, 6, 2) + " " +
                                     FloatToStrF(Traction->pPoint2.z, ffFixed, 6, 2));
                    }
        }
    }
    iConnection = 0; // teraz b�dzie licznikiem ko�c�w
    for (nCurrent = nRootOfType[TP_TRACTION]; nCurrent; nCurrent = nCurrent->nNext)
    { // operacje maj�ce na celu wykrywanie bie�ni wsp�lnych i ��czenie prz�se� napr��ania
        if (nCurrent->hvTraction->WhereIs()) // oznakowanie przedostatnich prz�se�
        { // poszukiwanie bie�ni wsp�lnej dla przedostatnich prz�se�, r�wnie� w celu po��czenia
            // zasilania
            // to si� nie sprawdza, bo po��czy� si� mog� dwa niezasilane odcinki jako najbli�sze
            // sobie
            // nCurrent->hvTraction->hvParallel=TractionNearestFind(nCurrent->pCenter,0,nCurrent);
            // //szukanie najbli�szego prz�s�a
            // trzeba by zlicza� ko�ce, a potem wpisa� je do tablicy, aby sukcesywnie pod��cza� do
            // zasilaczy
            nCurrent->hvTraction->iTries = 5; // oznaczanie ko�cowych
            ++iConnection;
        }
        if (nCurrent->hvTraction->fResistance[0] == 0.0)
        {
            nCurrent->hvTraction
                ->ResistanceCalc(); // obliczanie prz�se� w segmencie z bezpo�rednim zasilaniem
            // ErrorLog("Section "+nCurrent->hvTraction->asPowerSupplyName+" connected"); //jako
            // niby b��d b�dzie bardziej widoczne
            nCurrent->hvTraction->iTries = 0; // nie potrzeba mu szuka� zasilania
        }
        // if (!Traction->hvParallel) //jeszcze utworzy� p�tle z bie�ni wsp�lnych
    }
    int zg = 0; // zgodno�� kierunku prz�se�, tymczasowo iterator do tabeli ko�c�w
    TGroundNode **nEnds = new TGroundNode *[iConnection]; // ko�c�w jest ok. 10 razy mniej ni�
    // wszystkich prz�se� (Quark: 216)
    for (nCurrent = nRootOfType[TP_TRACTION]; nCurrent; nCurrent = nCurrent->nNext)
    { //��czenie bie�ni wsp�lnych, w tym oznaczanie niepodanych jawnie
        Traction = nCurrent->hvTraction;
        if (!Traction->asParallel.IsEmpty()) // b�dzie wska�nik na inne prz�s�o
            if ((Traction->asParallel == "none") ||
                (Traction->asParallel == "*")) // je�li nieokre�lone
                Traction->iLast =
                    2; // jakby przedostatni - niech po prostu szuka (iLast ju� przeliczone)
            else if (!Traction->hvParallel) // je�li jeszcze nie zosta� w��czony w k�ko
            {
                nTemp = FindGroundNode(Traction->asParallel, TP_TRACTION);
                if (nTemp)
                { // o ile zostanie znalezione prz�s�o o takiej nazwie
                    if (!nTemp->hvTraction
                             ->hvParallel) // je�li tamten jeszcze nie ma wska�nika bie�ni wsp�lnej
                        Traction->hvParallel =
                            nTemp->hvTraction; // wpisa� siebie i dalej da� mu wska�nik zwrotny
                    else // a jak ma, to albo do��czy� si� do k�eczka
                        Traction->hvParallel =
                            nTemp->hvTraction->hvParallel; // przj�� dotychczasowy wska�nik od niego
                    nTemp->hvTraction->hvParallel =
                        Traction; // i na koniec ustawienie wska�nika zwrotnego
                }
                if (!Traction->hvParallel)
                    ErrorLog("Missed overhead: " + Traction->asParallel); // logowanie braku
            }
        if (Traction->iTries > 0) // je�li zaznaczony do pod��czenia
            // if (!nCurrent->hvTraction->psPower[0]||!nCurrent->hvTraction->psPower[1])
            if (zg < iConnection) // zabezpieczenie
                nEnds[zg++] = nCurrent; // wype�nianie tabeli ko�c�w w celu szukania im po��cze�
    }
    while (zg < iConnection)
        nEnds[zg++] = NULL; // zape�nienie do ko�ca tablicy, je�li by jakie� ko�ce wypad�y
    zg = 1; // nieefektywny przebieg ko�czy ��czenie
    while (zg)
    { // ustalenie zast�pczej rezystancji dla ka�dego prz�s�a
        zg = 0; // flaga pod��czonych prz�se� ko�cowych: -1=puste wska�niki, 0=co� zosta�o,
        // 1=wykonano ��czenie
        for (int i = 0; i < iConnection; ++i)
            if (nEnds[i]) // za�atwione b�dziemy zerowa�
            { // ka�dy przebieg to pr�ba pod��czenia ko�ca segmentu napr�ania do innego zasilanego
                // prz�s�a
                if (nEnds[i]->hvTraction->hvNext[0])
                { // je�li ko�cowy ma ci�g dalszy od strony 0 (Point1), szukamy odcinka najbli�szego
                    // do Point2
                    if (TractionNearestFind(nEnds[i]->hvTraction->pPoint2, 0,
                                            nEnds[i])) // poszukiwanie prz�s�a
                    {
                        nEnds[i] = NULL;
                        zg = 1; // jak co� zosta�o pod��czone, to mo�e zasilanie gdzie� dodatkowo
                        // dotrze
                    }
                }
                else if (nEnds[i]->hvTraction->hvNext[1])
                { // je�li ko�cowy ma ci�g dalszy od strony 1 (Point2), szukamy odcinka najbli�szego
                    // do Point1
                    if (TractionNearestFind(nEnds[i]->hvTraction->pPoint1, 1,
                                            nEnds[i])) // poszukiwanie prz�s�a
                    {
                        nEnds[i] = NULL;
                        zg = 1; // jak co� zosta�o pod��czone, to mo�e zasilanie gdzie� dodatkowo
                        // dotrze
                    }
                }
                else
                { // gdy koniec jest samotny, to na razie nie zostanie pod��czony (nie powinno
                    // takich by�)
                    nEnds[i] = NULL;
                }
            }
    }
    delete[] nEnds; // nie potrzebne ju�
};

void TGround::TrackJoin(TGroundNode *Current)
{ // wyszukiwanie s�siednich tor�w do pod��czenia (wydzielone na u�ytek obrotnicy)
    TTrack *Track = Current->pTrack;
    TTrack *tmp;
    int iConnection;
    if (!Track->CurrentPrev())
    {
        tmp = FindTrack(Track->CurrentSegment()->FastGetPoint_0(), iConnection,
                        Current); // Current do pomini�cia
        switch (iConnection)
        {
        case 0:
            Track->ConnectPrevPrev(tmp, 0);
            break;
        case 1:
            Track->ConnectPrevNext(tmp, 1);
            break;
        }
    }
    if (!Track->CurrentNext())
    {
        tmp = FindTrack(Track->CurrentSegment()->FastGetPoint_1(), iConnection, Current);
        switch (iConnection)
        {
        case 0:
            Track->ConnectNextPrev(tmp, 0);
            break;
        case 1:
            Track->ConnectNextNext(tmp, 1);
            break;
        }
    }
}

// McZapkie-070602: wyzwalacze zdarzen
bool TGround::InitLaunchers()
{
    TGroundNode *Current, *tmp;
    TEventLauncher *EventLauncher;
    int i;
    for (Current = nRootOfType[TP_EVLAUNCH]; Current; Current = Current->nNext)
    {
        EventLauncher = Current->EvLaunch;
        if (EventLauncher->iCheckMask != 0)
            if (EventLauncher->asMemCellName != AnsiString("none"))
            { // je�li jest powi�zana kom�rka pami�ci
                tmp = FindGroundNode(EventLauncher->asMemCellName, TP_MEMCELL);
                if (tmp)
                    EventLauncher->MemCell = tmp->MemCell; // je�li znaleziona, dopisa�
                else
                    MessageBox(0, "Cannot find Memory Cell for Event Launcher", "Error", MB_OK);
            }
            else
                EventLauncher->MemCell = NULL;
        EventLauncher->Event1 = (EventLauncher->asEvent1Name != AnsiString("none")) ?
                                    FindEvent(EventLauncher->asEvent1Name) :
                                    NULL;
        EventLauncher->Event2 = (EventLauncher->asEvent2Name != AnsiString("none")) ?
                                    FindEvent(EventLauncher->asEvent2Name) :
                                    NULL;
    }
    return true;
}

TTrack * TGround::FindTrack(vector3 Point, int &iConnection, TGroundNode *Exclude)
{ // wyszukiwanie innego toru ko�cz�cego si� w (Point)
    TTrack *Track;
    TGroundNode *Current;
    TTrack *tmp;
    iConnection = -1;
    TSubRect *sr;
    // najpierw szukamy w okolicznych segmentach
    int c = GetColFromX(Point.x);
    int r = GetRowFromZ(Point.z);
    if ((sr = FastGetSubRect(c, r)) != NULL) // 75% tor�w jest w tym samym sektorze
        if ((tmp = sr->FindTrack(&Point, iConnection, Exclude->pTrack)) != NULL)
            return tmp;
    int i, x, y;
    for (i = 1; i < 9;
         ++i) // sektory w kolejno�ci odleg�o�ci, 4 jest tu wystarczaj�ce, 9 na wszelki wypadek
    { // niemal wszystkie pod��czone tory znajduj� si� w s�siednich 8 sektorach
        x = SectorOrder[i].x;
        y = SectorOrder[i].y;
        if ((sr = FastGetSubRect(c + y, r + x)) != NULL)
            if ((tmp = sr->FindTrack(&Point, iConnection, Exclude->pTrack)) != NULL)
                return tmp;
        if (x)
            if ((sr = FastGetSubRect(c + y, r - x)) != NULL)
                if ((tmp = sr->FindTrack(&Point, iConnection, Exclude->pTrack)) != NULL)
                    return tmp;
        if (y)
            if ((sr = FastGetSubRect(c - y, r + x)) != NULL)
                if ((tmp = sr->FindTrack(&Point, iConnection, Exclude->pTrack)) != NULL)
                    return tmp;
        if ((sr = FastGetSubRect(c - y, r - x)) != NULL)
            if ((tmp = sr->FindTrack(&Point, iConnection, Exclude->pTrack)) != NULL)
                return tmp;
    }
#if 0
 //wyszukiwanie czo�gowe (po wszystkich jak leci) - nie ma chyba sensu
 for (Current=nRootOfType[TP_TRACK];Current;Current=Current->Next)
 {
  if ((Current->iType==TP_TRACK) && (Current!=Exclude))
  {
   iConnection=Current->pTrack->TestPoint(&Point);
   if (iConnection>=0) return Current->pTrack;
  }
 }
#endif
    return NULL;
}

TTraction * TGround::FindTraction(vector3 *Point, int &iConnection, TGroundNode *Exclude)
{ // wyszukiwanie innego prz�s�a ko�cz�cego si� w (Point)
    TTraction *Traction;
    TGroundNode *Current;
    TTraction *tmp;
    iConnection = -1;
    TSubRect *sr;
    // najpierw szukamy w okolicznych segmentach
    int c = GetColFromX(Point->x);
    int r = GetRowFromZ(Point->z);
    if ((sr = FastGetSubRect(c, r)) != NULL) // wi�kszo�� b�dzie w tym samym sektorze
        if ((tmp = sr->FindTraction(Point, iConnection, Exclude->hvTraction)) != NULL)
            return tmp;
    int i, x, y;
    for (i = 1; i < 9;
         ++i) // sektory w kolejno�ci odleg�o�ci, 4 jest tu wystarczaj�ce, 9 na wszelki wypadek
    { // wszystkie prz�s�a powinny zosta� znajdowa� si� w s�siednich 8 sektorach
        x = SectorOrder[i].x;
        y = SectorOrder[i].y;
        if ((sr = FastGetSubRect(c + y, r + x)) != NULL)
            if ((tmp = sr->FindTraction(Point, iConnection, Exclude->hvTraction)) != NULL)
                return tmp;
        if (x & y)
        {
            if ((sr = FastGetSubRect(c + y, r - x)) != NULL)
                if ((tmp = sr->FindTraction(Point, iConnection, Exclude->hvTraction)) != NULL)
                    return tmp;
            if ((sr = FastGetSubRect(c - y, r + x)) != NULL)
                if ((tmp = sr->FindTraction(Point, iConnection, Exclude->hvTraction)) != NULL)
                    return tmp;
        }
        if ((sr = FastGetSubRect(c - y, r - x)) != NULL)
            if ((tmp = sr->FindTraction(Point, iConnection, Exclude->hvTraction)) != NULL)
                return tmp;
    }
    return NULL;
};

TTraction * TGround::TractionNearestFind(vector3 &p, int dir, TGroundNode *n)
{ // wyszukanie najbli�szego do (p) prz�s�a o tej samej nazwie sekcji (ale innego ni� pod��czone)
    // oraz zasilanego z kierunku (dir)
    TGroundNode *nCurrent, *nBest = NULL;
    int i, j, k, zg;
    double d, dist = 200.0 * 200.0; //[m] odleg�o�� graniczna
    // najpierw szukamy w okolicznych segmentach
    int c = GetColFromX(n->pCenter.x);
    int r = GetRowFromZ(n->pCenter.z);
    TSubRect *sr;
    for (i = -1; i <= 1; ++i) // przegl�damy 9 najbli�szych sektor�w
        for (j = -1; j <= 1; ++j) //
            if ((sr = FastGetSubRect(c + i, r + j)) != NULL) // o ile w og�le sektor jest
                for (nCurrent = sr->nRenderWires; nCurrent; nCurrent = nCurrent->nNext3)
                    if (nCurrent->iType == TP_TRACTION)
                        if (nCurrent->hvTraction->psSection ==
                            n->hvTraction->psSection) // je�li ta sama sekcja
                            if (nCurrent != n) // ale nie jest tym samym
                                if (nCurrent->hvTraction !=
                                    n->hvTraction
                                        ->hvNext[0]) // ale nie jest bezpo�rednio pod��czonym
                                    if (nCurrent->hvTraction != n->hvTraction->hvNext[1])
                                        if (nCurrent->hvTraction->psPower
                                                [k = (DotProduct(
                                                          n->hvTraction->vParametric,
                                                          nCurrent->hvTraction->vParametric) >= 0 ?
                                                          dir ^ 1 :
                                                          dir)]) // ma zasilanie z odpowiedniej
                                            // strony
                                            if (nCurrent->hvTraction->fResistance[k] >=
                                                0.0) //�eby si� nie propagowa�y jakie� ujemne
                                            { // znaleziony kandydat do po��czenia
                                                d = SquareMagnitude(
                                                    p -
                                                    nCurrent
                                                        ->pCenter); // kwadrat odleg�o�ci �rodk�w
                                                if (dist > d)
                                                { // zapami�tanie nowego najbli�szego
                                                    dist = d; // nowy rekord odleg�o�ci
                                                    nBest = nCurrent;
                                                    zg = k; // z kt�rego ko�ca bra� wska�nik
                                                    // zasilacza
                                                }
                                            }
    if (nBest) // jak znalezione prz�s�o z zasilaniem, to pod��czenie "r�wnoleg�e"
    {
        n->hvTraction->ResistanceCalc(dir, nBest->hvTraction->fResistance[zg],
                                      nBest->hvTraction->psPower[zg]);
        // testowo skrzywienie prz�s�a tak, aby pokaza� sk�d ma zasilanie
        // if (dir) //1 gdy ci�g dalszy jest od strony Point2
        // n->hvTraction->pPoint3=0.25*(nBest->pCenter+3*(zg?nBest->hvTraction->pPoint4:nBest->hvTraction->pPoint3));
        // else
        // n->hvTraction->pPoint4=0.25*(nBest->pCenter+3*(zg?nBest->hvTraction->pPoint4:nBest->hvTraction->pPoint3));
    }
    return (nBest ? nBest->hvTraction : NULL);
};

bool TGround::AddToQuery(TEvent *Event, TDynamicObject *Node)
{
    if (Event->bEnabled) // je�li mo�e by� dodany do kolejki (nie u�ywany w skanowaniu)
        if (!Event->iQueued) // je�li nie dodany jeszcze do kolejki
        { // kolejka event�w jest posortowana wzgl�dem (fStartTime)
            Event->Activator = Node;
            if (Event->Type == tp_AddValues ? (Event->fDelay == 0.0) : false)
            { // eventy AddValues trzeba wykonywa� natychmiastowo, inaczej kolejka mo�e zgubi�
                // jakie� dodawanie
                // Ra: kopiowanie wykonania tu jest bez sensu, lepiej by by�o wydzieli� funkcj�
                // wykonuj�c� eventy i j� wywo�a�
                if (EventConditon(Event))
                { // teraz mog� by� warunki do tych event�w
                    Event->Params[5].asMemCell->UpdateValues(
                        Event->Params[0].asText, Event->Params[1].asdouble,
                        Event->Params[2].asdouble, Event->iFlags);
                    if (Event->Params[6].asTrack)
                    { // McZapkie-100302 - updatevalues oprocz zmiany wartosci robi putcommand dla
                        // wszystkich 'dynamic' na danym torze
                        for (int i = 0; i < Event->Params[6].asTrack->iNumDynamics; ++i)
                            Event->Params[5].asMemCell->PutCommand(
                                Event->Params[6].asTrack->Dynamics[i]->Mechanik,
                                &Event->Params[4].nGroundNode->pCenter);
                        //if (DebugModeFlag)
                            WriteLog("EVENT EXECUTED: AddValues & Track command - " +
                                     AnsiString(Event->Params[0].asText) + " " +
                                     AnsiString(Event->Params[1].asdouble) + " " +
                                     AnsiString(Event->Params[2].asdouble));
                    }
                    //else if (DebugModeFlag)
                        WriteLog("EVENT EXECUTED: AddValues - " +
                                 AnsiString(Event->Params[0].asText) + " " +
                                 AnsiString(Event->Params[1].asdouble) + " " +
                                 AnsiString(Event->Params[2].asdouble));
                }
                Event =
                    Event
                        ->evJoined; // je�li jest kolejny o takiej samej nazwie, to idzie do kolejki
            }
            if (Event)
            { // standardowe dodanie do kolejki
                WriteLog("EVENT ADDED TO QUEUE: " + Event->asName +
                         (Node ? AnsiString(" by " + Node->asName) : AnsiString("")));
                Event->fStartTime =
                    fabs(Event->fDelay) + Timer::GetTime(); // czas od uruchomienia scenerii
                if (Event->fRandomDelay > 0.0)
                    Event->fStartTime += Event->fRandomDelay * random(10000) *
                                         0.0001; // doliczenie losowego czasu op�nienia
                ++Event->iQueued; // zabezpieczenie przed podw�jnym dodaniem do kolejki
                if (QueryRootEvent ? Event->fStartTime >= QueryRootEvent->fStartTime : false)
                    QueryRootEvent->AddToQuery(Event); // dodanie gdzie� w �rodku
                else
                { // dodanie z przodu: albo nic nie ma, albo ma by� wykonany szybciej ni� pierwszy
                    Event->evNext = QueryRootEvent;
                    QueryRootEvent = Event;
                }
            }
        }
    return true;
}

bool TGround::EventConditon(TEvent *e)
{ // sprawdzenie spelnienia warunk�w dla eventu
    if (e->iFlags <= update_only)
        return true; // bezwarunkowo
    if (e->iFlags & conditional_trackoccupied)
        return (!e->Params[9].asTrack->IsEmpty());
    else if (e->iFlags & conditional_trackfree)
        return (e->Params[9].asTrack->IsEmpty());
    else if (e->iFlags & conditional_propability)
    {
        double rprobability = 1.0 * rand() / RAND_MAX;
        WriteLog("Random integer: " + CurrToStr(rprobability) + "/" +
                 CurrToStr(e->Params[10].asdouble));
        return (e->Params[10].asdouble > rprobability);
    }
    else if (e->iFlags & conditional_memcompare)
    { // por�wnanie warto�ci
        if (tmpEvent->Params[9].asMemCell->Compare(e->Params[10].asText, e->Params[11].asdouble,
                                                   e->Params[12].asdouble, e->iFlags))
			{ //logowanie spe�nionych warunk�w
			LogComment = e->Params[9].asMemCell->Text() + AnsiString(" ") +
                         FloatToStrF(e->Params[9].asMemCell->Value1(), ffFixed, 8, 2) + " " +
                         FloatToStrF(tmpEvent->Params[9].asMemCell->Value2(), ffFixed, 8, 2) +
                         " = ";
            if (TestFlag(e->iFlags, conditional_memstring))
                LogComment += AnsiString(tmpEvent->Params[10].asText);
            else
                LogComment += "*";
            if (TestFlag(tmpEvent->iFlags, conditional_memval1))
                LogComment += " " + FloatToStrF(tmpEvent->Params[11].asdouble, ffFixed, 8, 2);
            else
                LogComment += " *";
            if (TestFlag(tmpEvent->iFlags, conditional_memval2))
                LogComment += " " + FloatToStrF(tmpEvent->Params[12].asdouble, ffFixed, 8, 2);
            else
                LogComment += " *";
			WriteLog(LogComment.c_str());
            return true;
			}
        //else if (Global::iWriteLogEnabled && DebugModeFlag) //zawsze bo to bardzo istotne w debugowaniu scenariuszy
		else
        { // nie zgadza si�, wi�c sprawdzmy, co
            LogComment = e->Params[9].asMemCell->Text() + AnsiString(" ") +
                         FloatToStrF(e->Params[9].asMemCell->Value1(), ffFixed, 8, 2) + " " +
                         FloatToStrF(tmpEvent->Params[9].asMemCell->Value2(), ffFixed, 8, 2) +
                         " != ";
            if (TestFlag(e->iFlags, conditional_memstring))
                LogComment += AnsiString(tmpEvent->Params[10].asText);
            else
                LogComment += "*";
            if (TestFlag(tmpEvent->iFlags, conditional_memval1))
                LogComment += " " + FloatToStrF(tmpEvent->Params[11].asdouble, ffFixed, 8, 2);
            else
                LogComment += " *";
            if (TestFlag(tmpEvent->iFlags, conditional_memval2))
                LogComment += " " + FloatToStrF(tmpEvent->Params[12].asdouble, ffFixed, 8, 2);
            else
                LogComment += " *";
            WriteLog(LogComment.c_str());
        }
    }
    return false;
};

bool TGround::CheckQuery()
{ // sprawdzenie kolejki event�w oraz wykonanie tych, kt�rym czas min��
    TLocation loc;
    int i;
    /* //Ra: to w og�le jaki� chory kod jest; wygl�da jak wyszukanie eventu z najlepszym czasem
     Double evtime,evlowesttime; //Ra: co to za typ?
     //evlowesttime=1000000;
     if (QueryRootEvent)
     {
      OldQRE=QueryRootEvent;
      tmpEvent=QueryRootEvent;
     }
     if (QueryRootEvent)
     {
      for (i=0;i<90;++i)
      {
       evtime=((tmpEvent->fStartTime)-(Timer::GetTime())); //pobranie warto�ci zmiennej
       if (evtime<evlowesttime)
       {
        evlowesttime=evtime;
        tmp2Event=tmpEvent;
       }
       if (tmpEvent->Next)
        tmpEvent=tmpEvent->Next;
       else
        i=100;
      }
      if (OldQRE!=tmp2Event)
      {
       QueryRootEvent->AddToQuery(QueryRootEvent);
       QueryRootEvent=tmp2Event;
      }
     }
    */
    /*
     if (QueryRootEvent)
     {//wypisanie kolejki
      tmpEvent=QueryRootEvent;
      WriteLog("--> Event queue:");
      while (tmpEvent)
      {
       WriteLog(tmpEvent->asName+" "+AnsiString(tmpEvent->fStartTime));
       tmpEvent=tmpEvent->Next;
      }
     }
    */
    while (QueryRootEvent ? QueryRootEvent->fStartTime < Timer::GetTime() : false)
    { // eventy s� posortowana wg czasu wykonania
        tmpEvent = QueryRootEvent; // wyj�cie eventu z kolejki
        if (QueryRootEvent->evJoined) // je�li jest kolejny o takiej samej nazwie
        { // to teraz on b�dzie nast�pny do wykonania
            QueryRootEvent = QueryRootEvent->evJoined; // nast�pny b�dzie ten doczepiony
            QueryRootEvent->evNext = tmpEvent->evNext; // pami�taj�c o nast�pnym z kolejki
            QueryRootEvent->fStartTime =
                tmpEvent->fStartTime; // czas musi by� ten sam, bo nie jest aktualizowany
            QueryRootEvent->Activator = tmpEvent->Activator; // pojazd aktywuj�cy
            // w sumie mo�na by go doda� normalnie do kolejki, ale trzeba te po��czone posortowa� wg
            // czasu wykonania
        }
        else // a jak nazwa jest unikalna, to kolejka idzie dalej
            QueryRootEvent = QueryRootEvent->evNext; // NULL w skrajnym przypadku
        if (tmpEvent->bEnabled)
        { // w zasadzie te wy��czone s� skanowane i nie powinny si� nigdy w kolejce znale��
            WriteLog("EVENT LAUNCHED: " + tmpEvent->asName +
                     (tmpEvent->Activator ? AnsiString(" by " + tmpEvent->Activator->asName) :
                                            AnsiString("")));
            switch (tmpEvent->Type)
            {
            case tp_CopyValues: // skopiowanie warto�ci z innej kom�rki
                tmpEvent->Params[5].asMemCell->UpdateValues(
                    tmpEvent->Params[9].asMemCell->Text(), tmpEvent->Params[9].asMemCell->Value1(),
                    tmpEvent->Params[9].asMemCell->Value2(),
                    tmpEvent->iFlags // flagi okre�laj�, co ma by� skopiowane
                    );
            // break; //�eby si� wys�a�o do tor�w i nie by�o potrzeby na AddValues * 0 0
            case tp_AddValues: // r�ni si� jedn� flag� od UpdateValues
            case tp_UpdateValues:
                if (EventConditon(tmpEvent))
                { // teraz mog� by� warunki do tych event�w
                    if (tmpEvent->Type != tp_CopyValues) // dla CopyValues zrobi�o si� wcze�niej
                        tmpEvent->Params[5].asMemCell->UpdateValues(
                            tmpEvent->Params[0].asText, tmpEvent->Params[1].asdouble,
                            tmpEvent->Params[2].asdouble, tmpEvent->iFlags);
                    if (tmpEvent->Params[6].asTrack)
                    { // McZapkie-100302 - updatevalues oprocz zmiany wartosci robi putcommand dla
                        // wszystkich 'dynamic' na danym torze
                        for (int i = 0; i < tmpEvent->Params[6].asTrack->iNumDynamics; ++i)
                            tmpEvent->Params[5].asMemCell->PutCommand(
                                tmpEvent->Params[6].asTrack->Dynamics[i]->Mechanik,
                                &tmpEvent->Params[4].nGroundNode->pCenter);
                        //if (DebugModeFlag)
                            WriteLog("Type: UpdateValues & Track command - " +
                                     AnsiString(tmpEvent->Params[0].asText) + " " +
                                     AnsiString(tmpEvent->Params[1].asdouble) + " " +
                                     AnsiString(tmpEvent->Params[2].asdouble));
                    }
                    else //if (DebugModeFlag)
                        WriteLog("Type: UpdateValues - " + AnsiString(tmpEvent->Params[0].asText) +
                                 " " + AnsiString(tmpEvent->Params[1].asdouble) + " " +
                                 AnsiString(tmpEvent->Params[2].asdouble));
                }
                break;
            case tp_GetValues:
                if (tmpEvent->Activator)
                {
                    // loc.X= -tmpEvent->Params[8].nGroundNode->pCenter.x;
                    // loc.Y=  tmpEvent->Params[8].nGroundNode->pCenter.z;
                    // loc.Z=  tmpEvent->Params[8].nGroundNode->pCenter.y;
                    if (Global::iMultiplayer) // potwierdzenie wykonania dla serwera (odczyt
                        // semafora ju� tak nie dzia�a)
                        WyslijEvent(tmpEvent->asName, tmpEvent->Activator->GetName());
                    // tmpEvent->Params[9].asMemCell->PutCommand(tmpEvent->Activator->Mechanik,loc);
                    tmpEvent->Params[9].asMemCell->PutCommand(
                        tmpEvent->Activator->Mechanik, &tmpEvent->Params[8].nGroundNode->pCenter);
                }
                WriteLog("Type: GetValues");
                break;
            case tp_PutValues:
                if (tmpEvent->Activator)
                {
                    loc.X =
                        -tmpEvent->Params[3].asdouble; // zamiana, bo fizyka ma inaczej ni� sceneria
                    loc.Y = tmpEvent->Params[5].asdouble;
                    loc.Z = tmpEvent->Params[4].asdouble;
                    if (tmpEvent->Activator->Mechanik) // przekazanie rozkazu do AI
                        tmpEvent->Activator->Mechanik->PutCommand(
                            tmpEvent->Params[0].asText, tmpEvent->Params[1].asdouble,
                            tmpEvent->Params[2].asdouble, loc);
                    else
                    { // przekazanie do pojazdu
                        tmpEvent->Activator->MoverParameters->PutCommand(
                            tmpEvent->Params[0].asText, tmpEvent->Params[1].asdouble,
                            tmpEvent->Params[2].asdouble, loc);
                    }
                }
                WriteLog("Type: PutValues");
                break;
            case tp_Lights:
                if (tmpEvent->Params[9].asModel)
                    for (i = 0; i < iMaxNumLights; i++)
                        if (tmpEvent->Params[i].asdouble >= 0) //-1 zostawia bez zmiany
                            tmpEvent->Params[9].asModel->LightSet(
                                i, tmpEvent->Params[i].asdouble); // teraz te� u�amek
                break;
            case tp_Visible:
                if (tmpEvent->Params[9].nGroundNode)
                    tmpEvent->Params[9].nGroundNode->bVisible = (tmpEvent->Params[i].asInt > 0);
                break;
            case tp_Velocity:
                Error("Not implemented yet :(");
                break;
            case tp_Exit:
                MessageBox(0, tmpEvent->asNodeName.c_str(), " THE END ", MB_OK);
                Global::iTextMode = -1; // wy��czenie takie samo jak sekwencja F10 -> Y
                return false;
            case tp_Sound:
                switch (tmpEvent->Params[0].asInt)
                { // trzy mo�liwe przypadki:
                case 0:
                    tmpEvent->Params[9].tsTextSound->Stop();
                    break;
                case 1:
                    tmpEvent->Params[9].tsTextSound->Play(
                        1, 0, true, tmpEvent->Params[9].tsTextSound->vSoundPosition);
                    break;
                case -1:
                    tmpEvent->Params[9].tsTextSound->Play(
                        1, DSBPLAY_LOOPING, true, tmpEvent->Params[9].tsTextSound->vSoundPosition);
                    break;
                }
                break;
            case tp_Disable:
                Error("Not implemented yet :(");
                break;
            case tp_Animation: // Marcin: dorobic translacje - Ra: dorobi�em ;-)
                if (tmpEvent->Params[0].asInt == 1)
                    tmpEvent->Params[9].asAnimContainer->SetRotateAnim(
                        vector3(tmpEvent->Params[1].asdouble, tmpEvent->Params[2].asdouble,
                                tmpEvent->Params[3].asdouble),
                        tmpEvent->Params[4].asdouble);
                else if (tmpEvent->Params[0].asInt == 2)
                    tmpEvent->Params[9].asAnimContainer->SetTranslateAnim(
                        vector3(tmpEvent->Params[1].asdouble, tmpEvent->Params[2].asdouble,
                                tmpEvent->Params[3].asdouble),
                        tmpEvent->Params[4].asdouble);
                else if (tmpEvent->Params[0].asInt == 4)
                    tmpEvent->Params[9].asModel->AnimationVND(
                        tmpEvent->Params[8].asPointer,
                        tmpEvent->Params[1].asdouble, // tu mog� by� dodatkowe parametry, np. od-do
                        tmpEvent->Params[2].asdouble, tmpEvent->Params[3].asdouble,
                        tmpEvent->Params[4].asdouble);
                break;
            case tp_Switch:
                if (tmpEvent->Params[9].asTrack)
                    tmpEvent->Params[9].asTrack->Switch(tmpEvent->Params[0].asInt,
                                                        tmpEvent->Params[1].asdouble,
                                                        tmpEvent->Params[2].asdouble);
                if (Global::iMultiplayer) // dajemy zna� do serwera o prze�o�eniu
                    WyslijEvent(tmpEvent->asName, ""); // wys�anie nazwy eventu prze��czajacego
                // Ra: bardziej by si� przyda�a nazwa toru, ale nie ma do niej st�d dost�pu
                break;
            case tp_TrackVel:
                if (tmpEvent->Params[9].asTrack)
                { // pr�dko�� na zwrotnicy mo�e by� ograniczona z g�ry we wpisie, wi�kszej si� nie
                    // ustawi eventem
                    WriteLog("type: TrackVel");
                    // WriteLog("Vel: ",tmpEvent->Params[0].asdouble);
                    tmpEvent->Params[9].asTrack->VelocitySet(tmpEvent->Params[0].asdouble);
                    if (DebugModeFlag) // wy�wietlana jest ta faktycznie ustawiona
                        WriteLog("vel: ", tmpEvent->Params[9].asTrack->VelocityGet());
                }
                break;
            case tp_DynVel:
                Error("Event \"DynVel\" is obsolete");
                break;
            case tp_Multiple:
            {
                bCondition = EventConditon(tmpEvent);
                if (bCondition || (tmpEvent->iFlags &
                                   conditional_anyelse)) // warunek spelniony albo by�o u�yte else
                {
                    WriteLog("Multiple passed");
                    for (i = 0; i < 8; ++i)
                    { // dodawane do kolejki w kolejno�ci zapisania
                        if (tmpEvent->Params[i].asEvent)
                            if (bCondition != bool(tmpEvent->iFlags & (conditional_else << i)))
                            {
                                if (tmpEvent->Params[i].asEvent != tmpEvent)
                                    AddToQuery(tmpEvent->Params[i].asEvent,
                                               tmpEvent->Activator); // normalnie doda�
                                else // je�li ma by� rekurencja
                                    if (tmpEvent->fDelay >=
                                        5.0) // to musi mie� sensowny okres powtarzania
                                    if (tmpEvent->iQueued < 2)
                                    { // trzeba zrobi� wyj�tek, aby event m�g� si� sam doda� do
                                        // kolejki, raz ju� jest, ale b�dzie usuni�ty
                                        // p�tla eventowa mo�e by� uruchomiona wiele razy, ale tylko
                                        // pierwsze uruchomienie zadzia�a
                                        tmpEvent->iQueued =
                                            0; // tymczasowo, aby by� ponownie dodany do kolejki
                                        AddToQuery(tmpEvent, tmpEvent->Activator);
                                        tmpEvent->iQueued =
                                            2; // kolejny raz ju� absolutnie nie dodawa�
                                    }
                            }
                    }
                    if (Global::iMultiplayer) // dajemy zna� do serwera o wykonaniu
                        if ((tmpEvent->iFlags & conditional_anyelse) ==
                            0) // jednoznaczne tylko, gdy nie by�o else
                        {
                            if (tmpEvent->Activator)
                                WyslijEvent(tmpEvent->asName, tmpEvent->Activator->GetName());
                            else
                                WyslijEvent(tmpEvent->asName, "");
                        }
                }
            }
            break;
            case tp_WhoIs: // pobranie nazwy poci�gu do kom�rki pami�ci
                if (tmpEvent->iFlags & update_load)
                { // je�li pytanie o �adunek
                    if (tmpEvent->iFlags & update_memadd) // je�li typ pojazdu
                        tmpEvent->Params[9].asMemCell->UpdateValues(
                            tmpEvent->Activator->MoverParameters->TypeName.c_str(), // typ pojazdu
                            0, // na razie nic
                            0, // na razie nic
                            tmpEvent->iFlags &
                                (update_memstring | update_memval1 | update_memval2));
                    else // je�li parametry �adunku
                        tmpEvent->Params[9].asMemCell->UpdateValues(
                            tmpEvent->Activator->MoverParameters->LoadType != "" ?
                                tmpEvent->Activator->MoverParameters->LoadType.c_str() :
                                "none", // nazwa �adunku
                            tmpEvent->Activator->MoverParameters->Load, // aktualna ilo��
                            tmpEvent->Activator->MoverParameters->MaxLoad, // maksymalna ilo��
                            tmpEvent->iFlags &
                                (update_memstring | update_memval1 | update_memval2));
                }
                else if (tmpEvent->iFlags & update_memadd)
                { // je�li miejsce docelowe pojazdu
                    tmpEvent->Params[9].asMemCell->UpdateValues(
                        tmpEvent->Activator->asDestination.c_str(), // adres docelowy
                        tmpEvent->Activator->DirectionGet(), // kierunek pojazdu wzgl�dem czo�a
                        // sk�adu (1=zgodny,-1=przeciwny)
                        tmpEvent->Activator->MoverParameters
                            ->Power, // moc pojazdu silnikowego: 0 dla wagonu
                        tmpEvent->iFlags & (update_memstring | update_memval1 | update_memval2));
                }
                else if (tmpEvent->Activator->Mechanik)
                    if (tmpEvent->Activator->Mechanik->Primary())
                    { // tylko je�li kto� tam siedzi - nie powinno dotyczy� pasa�era!
                        tmpEvent->Params[9].asMemCell->UpdateValues(
                            tmpEvent->Activator->Mechanik->TrainName().c_str(),
                            tmpEvent->Activator->Mechanik->StationCount() -
                                tmpEvent->Activator->Mechanik
                                    ->StationIndex(), // ile przystank�w do ko�ca
                            tmpEvent->Activator->Mechanik->IsStop() ? 1 :
                                                                      0, // 1, gdy ma tu zatrzymanie
                            tmpEvent->iFlags);
                        WriteLog("Train detected: " + tmpEvent->Activator->Mechanik->TrainName());
                    }
                break;
            case tp_LogValues: // zapisanie zawarto�ci kom�rki pami�ci do logu
                if (tmpEvent->Params[9].asMemCell) // je�li by�a podana nazwa kom�rki
                    WriteLog("Memcell \"" + tmpEvent->asNodeName + "\": " +
                             tmpEvent->Params[9].asMemCell->Text() + " " +
                             tmpEvent->Params[9].asMemCell->Value1() + " " +
                             tmpEvent->Params[9].asMemCell->Value2());
                else // lista wszystkich
                    for (TGroundNode *Current = nRootOfType[TP_MEMCELL]; Current;
                         Current = Current->nNext)
                        WriteLog("Memcell \"" + Current->asName + "\": " +
                                 Current->MemCell->Text() + " " + Current->MemCell->Value1() + " " +
                                 Current->MemCell->Value2());
                break;
            case tp_Voltage: // zmiana napi�cia w zasilaczu (TractionPowerSource)
                if (tmpEvent->Params[9].psPower)
                { // na razie takie chamskie ustawienie napi�cia zasilania
                    WriteLog("type: Voltage");
                    tmpEvent->Params[9].psPower->VoltageSet(tmpEvent->Params[0].asdouble);
                }
            case tp_Friction: // zmiana tarcia na scenerii
            { // na razie takie chamskie ustawienie napi�cia zasilania
                WriteLog("type: Friction");
                Global::fFriction = (tmpEvent->Params[0].asdouble);
            }
            break;
            case tp_Message: // wy�wietlenie komunikatu
                break;
            } // switch (tmpEvent->Type)
        } // if (tmpEvent->bEnabled)
        --tmpEvent->iQueued; // teraz moze by� ponownie dodany do kolejki
        /*
          if (QueryRootEvent->eJoined) //je�li jest kolejny o takiej samej nazwie
          {//to teraz jego dajemy do wykonania
           QueryRootEvent->eJoined->Next=QueryRootEvent->Next; //pami�taj�c o nast�pnym z kolejki
           QueryRootEvent->eJoined->fStartTime=QueryRootEvent->fStartTime; //czas musi by� ten sam,
          bo nie jest aktualizowany
           //QueryRootEvent->fStartTime=0;
           QueryRootEvent=QueryRootEvent->eJoined; //a wykona� ten doczepiony
          }
          else
          {//a jak nazwa jest unikalna, to kolejka idzie dalej
           //QueryRootEvent->fStartTime=0;
           QueryRootEvent=QueryRootEvent->Next; //NULL w skrajnym przypadku
          }
        */
    } // while
    return true;
}

void TGround::OpenGLUpdate(HDC hDC)
{
    SwapBuffers(hDC); // swap buffers (double buffering)
};

void TGround::UpdatePhys(double dt, int iter)
{ // aktualizacja fizyki sta�ym krokiem: dt=krok czasu [s], dt*iter=czas od ostatnich przelicze�
    for (TGroundNode *Current = nRootOfType[TP_TRACTIONPOWERSOURCE]; Current;
         Current = Current->nNext)
        Current->psTractionPowerSource->Update(dt * iter); // zerowanie sumy pr�d�w
};

bool TGround::Update(double dt, int iter)
{ // aktualizacja animacji krokiem FPS: dt=krok czasu [s], dt*iter=czas od ostatnich przelicze�
    if (dt == 0.0)
    { // je�li za��czona jest pauza, to tylko obs�u�y� ruch w kabinie trzeba
        return true;
    }
    // Ra: w zasadzie to trzeba by utworzy� oddzieln� list� taboru do liczenia fizyki
    //    na kt�r� by si� zapisywa�y wszystkie pojazdy b�d�ce w ruchu
    //    pojazdy stoj�ce nie potrzebuj� aktualizacji, chyba �e np. kto� im zmieni nastaw� hamulca
    //    oddzieln� list� mo�na by zrobi� na pojazdy z nap�dem, najlepiej posortowan� wg typu nap�du
    if (iter > 1) // ABu: ponizsze wykonujemy tylko jesli wiecej niz jedna iteracja
    { // pierwsza iteracja i wyznaczenie stalych:
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
        { //
            Current->DynamicObject->MoverParameters->ComputeConstans();
            Current->DynamicObject->CoupleDist();
            Current->DynamicObject->UpdateForce(dt, dt, false);
        }
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
            Current->DynamicObject->FastUpdate(dt);
        // pozostale iteracje
        for (int i = 1; i < (iter - 1); ++i) // je�li iter==5, to wykona si� 3 razy
        {
            for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
                Current->DynamicObject->UpdateForce(dt, dt, false);
            for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
                Current->DynamicObject->FastUpdate(dt);
        }
        // ABu 200205: a to robimy tylko raz, bo nie potrzeba wi�cej
        // Winger 180204 - pantografy
        double dt1 = dt * iter; // ca�kowity czas
        UpdatePhys(dt1, 1);
        TAnimModel::AnimUpdate(dt1); // wykonanie zakolejkowanych animacji
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
        { // Ra: zmieni� warunek na sprawdzanie pantograf�w w jednej zmiennej: czy pantografy i czy
            // podniesione
            if (Current->DynamicObject->MoverParameters->EnginePowerSource.SourceType ==
                CurrentCollector)
                GetTraction(Current->DynamicObject); // poszukiwanie drutu dla pantograf�w
            Current->DynamicObject->UpdateForce(dt, dt1, true); //,true);
        }
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
            Current->DynamicObject->Update(dt, dt1); // Ra 2015-01: tylko tu przelicza sie�
        // trakcyjn�
    }
    else
    { // jezeli jest tylko jedna iteracja
        UpdatePhys(dt, 1);
        TAnimModel::AnimUpdate(dt); // wykonanie zakolejkowanych animacji
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
        {
            if (Current->DynamicObject->MoverParameters->EnginePowerSource.SourceType ==
                CurrentCollector)
                GetTraction(Current->DynamicObject);
            Current->DynamicObject->MoverParameters->ComputeConstans();
            Current->DynamicObject->CoupleDist();
            Current->DynamicObject->UpdateForce(dt, dt, true); //,true);
        }
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
            Current->DynamicObject->Update(dt, dt); // Ra 2015-01: tylko tu przelicza sie� trakcyjn�
    }
    if (bDynamicRemove)
    { // je�li jest co� do usuni�cia z listy, to trzeba na ko�cu
        for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
            if (!Current->DynamicObject->bEnabled)
            {
                DynamicRemove(Current->DynamicObject); // usuni�cie tego i pod��czonych
                Current = nRootDynamic; // sprawdzanie listy od pocz�tku
            }
        bDynamicRemove = false; // na razie koniec
    }
    return true;
};

// Winger 170204 - szukanie trakcji nad pantografami
bool TGround::GetTraction(TDynamicObject *model)
{ // aktualizacja drutu zasilaj�cego dla ka�dego pantografu, �eby odczyta� napi�cie
    // je�li pojazd si� nie porusza, to nie ma sensu przelicza� tego wi�cej ni� raz
    double fRaParam; // parametr r�wnania parametrycznego odcinka drutu
    double fVertical; // odleg�o�� w pionie; musi by� w zasi�gu ruchu "pionowego" pantografu
    double fHorizontal; // odleg�o�� w bok; powinna by� mniejsza ni� p� szeroko�ci pantografu
    vector3 vLeft, vUp, vFront, dwys;
    vector3 pant0;
    vector3 vParam; // wsp�czynniki r�wnania parametrycznego drutu
    vector3 vStyk; // punkt przebicia drutu przez p�aszczyzn� ruchu pantografu
    vector3 vGdzie; // wektor po�o�enia drutu wzgl�dem pojazdu
    vFront = model->VectorFront(); // wektor normalny dla p�aszczyzny ruchu pantografu
    vUp = model->VectorUp(); // wektor pionu pud�a (pochylony od pionu na przechy�ce)
    vLeft = model->VectorLeft(); // wektor odleg�o�ci w bok (odchylony od poziomu na przechy�ce)
    dwys = model->GetPosition(); // wsp�rz�dne �rodka pojazdu
    TAnimPant *p; // wska�nik do obiektu danych pantografu
    for (int k = 0; k < model->iAnimType[ANIM_PANTS]; ++k)
    { // p�tla po pantografach
        p = model->pants[k].fParamPants;
        if (k ? model->MoverParameters->PantRearUp : model->MoverParameters->PantFrontUp)
        { // je�li pantograf podniesiony
            pant0 = dwys + (vLeft * p->vPos.z) + (vUp * p->vPos.y) + (vFront * p->vPos.x);
            if (p->hvPowerWire)
            { // je�eli znamy drut z poprzedniego przebiegu
                int n = 30; //�eby si� nie zap�tli�
                while (p->hvPowerWire)
                { // powtarzane a� do znalezienia odpowiedniego odcinka na li�cie dwukierunkowej
                    // obliczamy wyraz wolny r�wnania p�aszczyzny (to miejsce nie jest odpowienie)
                    vParam = p->hvPowerWire->vParametric; // wsp�czynniki r�wnania parametrycznego
                    fRaParam = -DotProduct(pant0, vFront);
                    // podstawiamy r�wnanie parametryczne drutu do r�wnania p�aszczyzny pantografu
                    // vFront.x*(t1x+t*vParam.x)+vFront.y*(t1y+t*vParam.y)+vFront.z*(t1z+t*vParam.z)+fRaDist=0;
                    fRaParam = -(DotProduct(p->hvPowerWire->pPoint1, vFront) + fRaParam) /
                               DotProduct(vParam, vFront);
                    if (fRaParam <
                        -0.001) // histereza rz�du 7cm na 70m typowego prz�s�a daje 1 promil
                        p->hvPowerWire = p->hvPowerWire->hvNext[0];
                    else if (fRaParam > 1.001)
                        p->hvPowerWire = p->hvPowerWire->hvNext[1];
                    else if (p->hvPowerWire->iLast & 3)
                    { // dla ostatniego i przedostatniego prz�s�a wymuszamy szukanie innego
                        p->hvPowerWire = NULL; // nie to, �e nie ma, ale trzeba sprawdzi� inne
                        // p->fHorizontal=fHorizontal; //zapami�tanie po�o�enia drutu
                        break;
                    }
                    else if (p->hvPowerWire->hvParallel)
                    { // je�li prz�s�o tworzy bie�ni� wsp�ln�, to trzeba sprawdzi� pozosta�e
                        p->hvPowerWire = NULL; // nie to, �e nie ma, ale trzeba sprawdzi� inne
                        // p->fHorizontal=fHorizontal; //zapami�tanie po�o�enia drutu
                        break; // tymczasowo dla bie�ni wsp�lnych poszukiwanie po ca�o�ci
                    }
                    else
                    { // je�li t jest w przedziale, wyznaczy� odleg�o�� wzd�u� wektor�w vUp i vLeft
                        vStyk = p->hvPowerWire->pPoint1 + fRaParam * vParam; // punkt styku
                        // p�aszczyzny z drutem
                        // (dla generatora �uku
                        // el.)
                        vGdzie = vStyk - pant0; // wektor
                        // odleg�o�� w pionie musi by� w zasi�gu ruchu "pionowego" pantografu
                        fVertical = DotProduct(
                            vGdzie, vUp); // musi si� mie�ci� w przedziale ruchu pantografu
                        // odleg�o�� w bok powinna by� mniejsza ni� p� szeroko�ci pantografu
                        fHorizontal = fabs(DotProduct(vGdzie, vLeft)) -
                                      p->fWidth; // to si� musi mie�ci� w przedziale zale�nym od
                        // szeroko�ci pantografu
                        // je�li w pionie albo w bok jest za daleko, to dany drut jest nieu�yteczny
                        if (fHorizontal > 0) // 0.635 dla AKP-1 AKP-4E
                        { // drut wyszed� poza zakres roboczy, ale jeszcze jest nabie�nik -
                            // pantograf si� unosi bez utraty pr�du
                            if (fHorizontal > p->fWidthExtra) // czy wyszed� za nabie�nik
                            {
                                p->hvPowerWire = NULL; // dotychczasowy drut nie liczy si�
                                // p->fHorizontal=fHorizontal; //zapami�tanie po�o�enia drutu
                            }
                            else
                            { // problem jest, gdy nowy drut jest wy�ej, wtedy pantograf od��cza si�
                                // od starego, a na podniesienie do nowego potrzebuje czasu
                                p->PantTraction =
                                    fVertical +
                                    0.15 * fHorizontal / p->fWidthExtra; // na razie liniowo na
                                // nabie�niku, dok�adno��
                                // poprawi si� p�niej
                                // p->fHorizontal=fHorizontal; //zapami�tanie po�o�enia drutu
                            }
                        }
                        else
                        { // po wyselekcjonowaniu drutu, przypisa� go do toru, �eby nie trzeba by�o
                            // szuka�
                            // dla 3 ko�cowych prz�se� sprawdzi� wszystkie dost�pne prz�s�a
                            // bo mog� by� umieszczone r�wnolegle nad torem - po��czy� w pier�cie�
                            // najlepiej, jakby odcinki r�wnoleg�e by�y oznaczone we wpisach
                            // WriteLog("Drut: "+AnsiString(fHorizontal)+" "+AnsiString(fVertical));
                            p->PantTraction = fVertical;
                            // p->fHorizontal=fHorizontal; //zapami�tanie po�o�enia drutu
                            break; // koniec p�tli, aktualny drut pasuje
                        }
                    }
                    if (--n <= 0) // co� za d�ugo to szukanie trwa
                        p->hvPowerWire = NULL;
                }
            }
            if (!p->hvPowerWire) // else nie, bo m�g� zosta� wyrzucony
            { // poszukiwanie po okolicznych sektorach
                int c = GetColFromX(dwys.x) + 1;
                int r = GetRowFromZ(dwys.z) + 1;
                TSubRect *tmp;
                TGroundNode *node;
                p->PantTraction = 5.0; // taka za du�a warto��
                for (int j = r - 2; j <= r; j++)
                    for (int i = c - 2; i <= c; i++)
                    { // poszukiwanie po najbli�szych sektorach niewiele da przy wi�kszym
                        // zag�szczeniu
                        tmp = FastGetSubRect(i, j);
                        if (tmp)
                        { // dany sektor mo�e nie mie� nic w �rodku
                            for (node = tmp->nRenderWires; node;
                                 node = node->nNext3) // nast�pny z grupy
                                if (node->iType ==
                                    TP_TRACTION) // w grupie tej s� druty oraz inne linie
                                {
                                    vParam =
                                        node->hvTraction
                                            ->vParametric; // wsp�czynniki r�wnania parametrycznego
                                    fRaParam = -DotProduct(pant0, vFront);
                                    fRaParam = -(DotProduct(node->hvTraction->pPoint1, vFront) +
                                                 fRaParam) /
                                               DotProduct(vParam, vFront);
                                    if ((fRaParam >= -0.001) ? (fRaParam <= 1.001) : false)
                                    { // je�li tylko jest w przedziale, wyznaczy� odleg�o�� wzd�u�
                                        // wektor�w vUp i vLeft
                                        vStyk = node->hvTraction->pPoint1 +
                                                fRaParam * vParam; // punkt styku p�aszczyzny z
                                        // drutem (dla generatora �uku
                                        // el.)
                                        vGdzie = vStyk - pant0; // wektor
                                        fVertical = DotProduct(
                                            vGdzie,
                                            vUp); // musi si� mie�ci� w przedziale ruchu pantografu
                                        if (fVertical >= 0.0) // je�li ponad pantografem (bo mo�e
                                            // �apa� druty spod wiaduktu)
                                            if (Global::bEnableTraction ?
                                                    fVertical < p->PantWys - 0.15 :
                                                    false) // je�li drut jest ni�ej ni� 15cm pod
                                            // �lizgiem
                                            { // prze��czamy w tryb po�amania, o ile jedzie;
                                                // (bEnableTraction) aby da�o si� je�dzi� na
                                                // ko�lawych
                                                // sceneriach
                                                fHorizontal = fabs(DotProduct(vGdzie, vLeft)) -
                                                              p->fWidth; // i do tego jeszcze
                                                // wejdzie pod �lizg
                                                if (fHorizontal <= 0.0) // 0.635 dla AKP-1 AKP-4E
                                                {
                                                    p->PantWys =
                                                        -1.0; // ujemna liczba oznacza po�amanie
                                                    p->hvPowerWire = NULL; // bo inaczej si� zasila
                                                    // w niesko�czono�� z
                                                    // po�amanego
                                                    // p->fHorizontal=fHorizontal; //zapami�tanie
                                                    // po�o�enia drutu
                                                    if (model->MoverParameters->EnginePowerSource
                                                            .CollectorParameters.CollectorsNo >
                                                        0) // liczba pantograf�w
                                                        --model->MoverParameters->EnginePowerSource
                                                              .CollectorParameters
                                                              .CollectorsNo; // teraz b�dzie
                                                    // mniejsza
                                                    if (DebugModeFlag)
                                                        ErrorLog(
                                                            "Pant. break: at " +
                                                            FloatToStrF(pant0.x, ffFixed, 7, 2) +
                                                            " " +
                                                            FloatToStrF(pant0.y, ffFixed, 7, 2) +
                                                            " " +
                                                            FloatToStrF(pant0.z, ffFixed, 7, 2));
                                                }
                                            }
                                            else if (fVertical < p->PantTraction) // ale ni�ej, ni�
                                            // poprzednio
                                            // znaleziony
                                            {
                                                fHorizontal =
                                                    fabs(DotProduct(vGdzie, vLeft)) - p->fWidth;
                                                if (fHorizontal <= 0.0) // 0.635 dla AKP-1 AKP-4E
                                                { // to si� musi mie�ci� w przedziale zaleznym od
                                                    // szeroko�ci pantografu
                                                    p->hvPowerWire =
                                                        node->hvTraction; // jaki� znaleziony
                                                    p->PantTraction =
                                                        fVertical; // zapami�tanie nowej wysoko�ci
                                                    // p->fHorizontal=fHorizontal; //zapami�tanie
                                                    // po�o�enia drutu
                                                }
                                                else if (fHorizontal <
                                                         p->fWidthExtra) // czy zmie�ci� si� w
                                                // zakresie nabie�nika?
                                                { // problem jest, gdy nowy drut jest wy�ej, wtedy
                                                    // pantograf od��cza si� od starego, a na
                                                    // podniesienie do nowego potrzebuje czasu
                                                    fVertical +=
                                                        0.15 * fHorizontal /
                                                        p->fWidthExtra; // korekta wysoko�ci o
                                                    // nabie�nik - drut nad
                                                    // nabie�nikiem jest
                                                    // geometrycznie jakby nieco
                                                    // wy�ej
                                                    if (fVertical <
                                                        p->PantTraction) // gdy po korekcie jest
                                                    // ni�ej, ni� poprzednio
                                                    // znaleziony
                                                    { // gdyby to wystarczy�o, to mo�emy go uzna�
                                                        p->hvPowerWire =
                                                            node->hvTraction; // mo�e by�
                                                        p->PantTraction =
                                                            fVertical; // na razie liniowo na
                                                        // nabie�niku, dok�adno��
                                                        // poprawi si� p�niej
                                                        // p->fHorizontal=fHorizontal;
                                                        // //zapami�tanie po�o�enia drutu
                                                    }
                                                }
                                            }
                                    } // warunek na parametr drutu <0;1>
                                } // p�tla po drutach
                        } // sektor istnieje
                    } // p�tla po sektorach
            } // koniec poszukiwania w sektorach
            if (!p->hvPowerWire) // je�li drut nie znaleziony
                if (!Global::bLiveTraction) // ale mo�na oszukiwa�
                    model->pants[k].fParamPants->PantTraction = 1.4; // to dajemy co� tam dla picu
        } // koniec obs�ugi podniesionego
        else
            p->hvPowerWire = NULL; // pantograf opuszczony
    }
    // if (model->fWahaczeAmp<model->MoverParameters->DistCounter)
    //{//nieu�ywana normalnie zmienna ogranicza powt�rzone logowania
    // model->fWahaczeAmp=model->MoverParameters->DistCounter;
    // ErrorLog(FloatToStrF(1000.0*model->MoverParameters->DistCounter,ffFixed,7,3)+","+FloatToStrF(p->PantTraction,ffFixed,7,3)+","+FloatToStrF(p->fHorizontal,ffFixed,7,3)+","+FloatToStrF(p->PantWys,ffFixed,7,3)+","+AnsiString(p->hvPowerWire?1:0));
    // //
    // if (p->fHorizontal>1.0)
    //{
    // //Global::iPause|=1; //zapauzowanie symulacji
    // Global::fTimeSpeed=1; //spowolnienie czasu do obejrzenia pantografu
    // return true; //�apacz
    //}
    //}
    return true;
};

bool TGround::RenderDL(vector3 pPosition)
{ // renderowanie scenerii z Display List - faza nieprzezroczystych
    glDisable(GL_BLEND);
    glAlphaFunc(GL_GREATER, 0.45); // im mniejsza warto��, tym wi�ksza ramka, domy�lnie 0.1f
    ++TGroundRect::iFrameNumber; // zwi�szenie licznika ramek (do usuwniania nadanimacji)
    CameraDirection.x = sin(Global::pCameraRotation); // wektor kierunkowy
    CameraDirection.z = cos(Global::pCameraRotation);
    int tr, tc;
    TGroundNode *node;
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_LIGHTING);
    int n = 2 * iNumSubRects; //(2*==2km) promie� wy�wietlanej mapy w sektorach
    int c = GetColFromX(pPosition.x);
    int r = GetRowFromZ(pPosition.z);
    TSubRect *tmp;
    for (node = srGlobal.nRenderHidden; node; node = node->nNext3)
        node->RenderHidden(); // rednerowanie globalnych (nie za cz�sto?)
    int i, j, k;
    // renderowanie czo�gowe dla obiekt�w aktywnych a niewidocznych
    for (j = r - n; j <= r + n; j++)
        for (i = c - n; i <= c + n; i++)
            if ((tmp = FastGetSubRect(i, j)) != NULL)
            {
                tmp->LoadNodes(); // oznaczanie aktywnych sektor�w
                for (node = tmp->nRenderHidden; node; node = node->nNext3)
                    node->RenderHidden();
                tmp->RenderSounds(); // jeszcze d�wi�ki pojazd�w by si� przyda�y, r�wnie�
                // niewidocznych
            }
    // renderowanie progresywne - zale�ne od FPS oraz kierunku patrzenia
    iRendered = 0; // ilo�� renderowanych sektor�w
    vector3 direction;
    for (k = 0; k < Global::iSegmentsRendered; ++k) // sektory w kolejno�ci odleg�o�ci
    { // przerobione na u�ycie SectorOrder
        i = SectorOrder[k].x; // na starcie oba >=0
        j = SectorOrder[k].y;
        do
        {
            if (j <= 0)
                i = -i; // pierwszy przebieg: j<=0, i>=0; drugi: j>=0, i<=0; trzeci: j<=0, i<=0
            // czwarty: j>=0, i>=0;
            j = -j; // i oraz j musi by� zmienione wcze�niej, �eby continue dzia�a�o
            direction = vector3(i, 0, j); // wektor od kamery do danego sektora
            if (LengthSquared3(direction) > 5) // te blisko s� zawsze wy�wietlane
            {
                direction = SafeNormalize(direction); // normalizacja
                if (CameraDirection.x * direction.x + CameraDirection.z * direction.z < 0.55)
                    continue; // pomijanie sektor�w poza k�tem patrzenia
            }
            Rects[(i + c) / iNumSubRects][(j + r) / iNumSubRects]
                .RenderDL(); // kwadrat kilometrowy nie zawsze, bo szkoda FPS
            if ((tmp = FastGetSubRect(i + c, j + r)) != NULL)
                if (tmp->iNodeCount) // o ile s� jakie� obiekty, bo po co puste sektory przelatywa�
                    pRendered[iRendered++] = tmp; // tworzenie listy sektor�w do renderowania
        } while ((i < 0) || (j < 0)); // s� 4 przypadki, opr�cz i=j=0
    }
    for (i = 0; i < iRendered; i++)
        pRendered[i]->RenderDL(); // renderowanie nieprzezroczystych
    return true;
}

bool TGround::RenderAlphaDL(vector3 pPosition)
{ // renderowanie scenerii z Display List - faza przezroczystych
    glEnable(GL_BLEND);
    glAlphaFunc(GL_GREATER, 0.04); // im mniejsza warto��, tym wi�ksza ramka, domy�lnie 0.1f
    TGroundNode *node;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    TSubRect *tmp;
    // Ra: renderowanie progresywne - zale�ne od FPS oraz kierunku patrzenia
    int i;
    for (i = iRendered - 1; i >= 0; --i) // od najdalszych
    { // przezroczyste tr�jk�ty w oddzielnym cyklu przed modelami
        tmp = pRendered[i];
        for (node = tmp->nRenderRectAlpha; node; node = node->nNext3)
            node->RenderAlphaDL(); // przezroczyste modele
    }
    for (i = iRendered - 1; i >= 0; --i) // od najdalszych
    { // renderowanie przezroczystych modeli oraz pojazd�w
        pRendered[i]->RenderAlphaDL();
    }
    glDisable(GL_LIGHTING); // linie nie powinny �wieci�
    for (i = iRendered - 1; i >= 0; --i) // od najdalszych
    { // druty na ko�cu, �eby si� nie robi�y bia�e plamy na tle lasu
        tmp = pRendered[i];
        for (node = tmp->nRenderWires; node; node = node->nNext3)
            node->RenderAlphaDL(); // druty
    }
    return true;
}

bool TGround::RenderVBO(vector3 pPosition)
{ // renderowanie scenerii z VBO - faza nieprzezroczystych
    glDisable(GL_BLEND);
    glAlphaFunc(GL_GREATER, 0.45); // im mniejsza warto��, tym wi�ksza ramka, domy�lnie 0.1f
    ++TGroundRect::iFrameNumber; // zwi�szenie licznika ramek
    CameraDirection.x = sin(Global::pCameraRotation); // wektor kierunkowy
    CameraDirection.z = cos(Global::pCameraRotation);
    int tr, tc;
    TGroundNode *node;
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_LIGHTING);
    int n = 2 * iNumSubRects; //(2*==2km) promie� wy�wietlanej mapy w sektorach
    int c = GetColFromX(pPosition.x);
    int r = GetRowFromZ(pPosition.z);
    TSubRect *tmp;
    for (node = srGlobal.nRenderHidden; node; node = node->nNext3)
        node->RenderHidden(); // rednerowanie globalnych (nie za cz�sto?)
    int i, j, k;
    // renderowanie czo�gowe dla obiekt�w aktywnych a niewidocznych
    for (j = r - n; j <= r + n; j++)
        for (i = c - n; i <= c + n; i++)
        {
            if ((tmp = FastGetSubRect(i, j)) != NULL)
            {
                for (node = tmp->nRenderHidden; node; node = node->nNext3)
                    node->RenderHidden();
                tmp->RenderSounds(); // jeszcze d�wi�ki pojazd�w by si� przyda�y, r�wnie�
                // niewidocznych
            }
        }
    // renderowanie progresywne - zale�ne od FPS oraz kierunku patrzenia
    iRendered = 0; // ilo�� renderowanych sektor�w
    vector3 direction;
    for (k = 0; k < Global::iSegmentsRendered; ++k) // sektory w kolejno�ci odleg�o�ci
    { // przerobione na u�ycie SectorOrder
        i = SectorOrder[k].x; // na starcie oba >=0
        j = SectorOrder[k].y;
        do
        {
            if (j <= 0)
                i = -i; // pierwszy przebieg: j<=0, i>=0; drugi: j>=0, i<=0; trzeci: j<=0, i<=0
            // czwarty: j>=0, i>=0;
            j = -j; // i oraz j musi by� zmienione wcze�niej, �eby continue dzia�a�o
            direction = vector3(i, 0, j); // wektor od kamery do danego sektora
            if (LengthSquared3(direction) > 5) // te blisko s� zawsze wy�wietlane
            {
                direction = SafeNormalize(direction); // normalizacja
                if (CameraDirection.x * direction.x + CameraDirection.z * direction.z < 0.55)
                    continue; // pomijanie sektor�w poza k�tem patrzenia
            }
            Rects[(i + c) / iNumSubRects][(j + r) / iNumSubRects]
                .RenderVBO(); // kwadrat kilometrowy nie zawsze, bo szkoda FPS
            if ((tmp = FastGetSubRect(i + c, j + r)) != NULL)
                if (tmp->iNodeCount) // je�eli s� jakie� obiekty, bo po co puste sektory przelatywa�
                    pRendered[iRendered++] = tmp; // tworzenie listy sektor�w do renderowania
        } while ((i < 0) || (j < 0)); // s� 4 przypadki, opr�cz i=j=0
    }
    // doda� rednerowanie terenu z E3D - jedno VBO jest u�ywane dla ca�ego modelu, chyba �e jest ich
    // wi�cej
    if (Global::pTerrainCompact)
        Global::pTerrainCompact->TerrainRenderVBO(TGroundRect::iFrameNumber);
    for (i = 0; i < iRendered; i++)
    { // renderowanie nieprzezroczystych
        pRendered[i]->RenderVBO();
    }
    return true;
}

bool TGround::RenderAlphaVBO(vector3 pPosition)
{ // renderowanie scenerii z VBO - faza przezroczystych
    glEnable(GL_BLEND);
    glAlphaFunc(GL_GREATER, 0.04); // im mniejsza warto��, tym wi�ksza ramka, domy�lnie 0.1f
    TGroundNode *node;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    TSubRect *tmp;
    int i;
    for (i = iRendered - 1; i >= 0; --i) // od najdalszych
    { // renderowanie przezroczystych tr�jk�t�w sektora
        tmp = pRendered[i];
        tmp->LoadNodes(); // ewentualne tworzenie siatek
        if (tmp->StartVBO())
        {
            for (node = tmp->nRenderRectAlpha; node; node = node->nNext3)
                if (node->iVboPtr >= 0)
                    node->RenderAlphaVBO(); // nieprzezroczyste obiekty terenu
            tmp->EndVBO();
        }
    }
    for (i = iRendered - 1; i >= 0; --i) // od najdalszych
        pRendered[i]->RenderAlphaVBO(); // przezroczyste modeli oraz pojazdy
    glDisable(GL_LIGHTING); // linie nie powinny �wieci�
    for (i = iRendered - 1; i >= 0; --i) // od najdalszych
    { // druty na ko�cu, �eby si� nie robi�y bia�e plamy na tle lasu
        tmp = pRendered[i];
        if (tmp->StartVBO())
        {
            for (node = tmp->nRenderWires; node; node = node->nNext3)
                node->RenderAlphaVBO(); // przezroczyste modele
            tmp->EndVBO();
        }
    }
    return true;
};

//---------------------------------------------------------------------------
void TGround::Navigate(String ClassName, UINT Msg, WPARAM wParam, LPARAM lParam)
{ // wys�anie komunikatu do steruj�cego
    HWND h = FindWindow(ClassName.c_str(), 0); // mo�na by to zapami�ta�
    if (h == 0)
        h = FindWindow(0, ClassName.c_str()); // mo�na by to zapami�ta�
    SendMessage(h, Msg, wParam, lParam);
};
//--------------------------------
void TGround::WyslijEvent(const AnsiString &e, const AnsiString &d)
{ // Ra: jeszcze do wyczyszczenia
    DaneRozkaz r;
    r.iSygn = 'EU07';
    r.iComm = 2; // 2 - event
    int i = e.Length(), j = d.Length();
    r.cString[0] = char(i);
    strcpy(r.cString + 1, e.c_str()); // zako�czony zerem
    r.cString[i + 2] = char(j); // licznik po zerze ko�cz�cym
    strcpy(r.cString + 3 + i, d.c_str()); // zako�czony zerem
    COPYDATASTRUCT cData;
    cData.dwData = 'EU07'; // sygnatura
    cData.cbData = 12 + i + j; // 8+dwa liczniki i dwa zera ko�cz�ce
    cData.lpData = &r;
    Navigate("TEU07SRK", WM_COPYDATA, (WPARAM)Global::hWnd, (LPARAM)&cData);
	CommLog(AnsiString(Now()) + " " + IntToStr(r.iComm) + " " + e + " sent");
};
//---------------------------------------------------------------------------
void TGround::WyslijUszkodzenia(const AnsiString &t, char fl)
{ // wys�anie informacji w postaci pojedynczego tekstu
	DaneRozkaz r;
	r.iSygn = 'EU07';
	r.iComm = 13; // numer komunikatu
	int i = t.Length();
	r.cString[0] = char(fl);
	r.cString[1] = char(i);
	strcpy(r.cString + 2, t.c_str()); // z zerem ko�cz�cym
	COPYDATASTRUCT cData;
	cData.dwData = 'EU07'; // sygnatura
	cData.cbData = 11 + i; // 8+licznik i zero ko�cz�ce
	cData.lpData = &r;
	Navigate("TEU07SRK", WM_COPYDATA, (WPARAM)Global::hWnd, (LPARAM)&cData);
	CommLog(AnsiString(Now()) + " " + IntToStr(r.iComm) + " " + t + " sent");
};
//---------------------------------------------------------------------------
void TGround::WyslijString(const AnsiString &t, int n)
{ // wys�anie informacji w postaci pojedynczego tekstu
    DaneRozkaz r;
    r.iSygn = 'EU07';
    r.iComm = n; // numer komunikatu
    int i = t.Length();
    r.cString[0] = char(i);
    strcpy(r.cString + 1, t.c_str()); // z zerem ko�cz�cym
    COPYDATASTRUCT cData;
    cData.dwData = 'EU07'; // sygnatura
    cData.cbData = 10 + i; // 8+licznik i zero ko�cz�ce
    cData.lpData = &r;
    Navigate("TEU07SRK", WM_COPYDATA, (WPARAM)Global::hWnd, (LPARAM)&cData);
	CommLog(AnsiString(Now()) + " " + IntToStr(r.iComm) + " " + t + " sent");
};
//---------------------------------------------------------------------------
void TGround::WyslijWolny(const AnsiString &t)
{ // Ra: jeszcze do wyczyszczenia
    WyslijString(t, 4); // tor wolny
};
//--------------------------------
void TGround::WyslijNamiary(TGroundNode *t)
{ // wys�anie informacji o poje�dzie - (float), d�ugo�� ramki b�dzie zwi�kszana w miar� potrzeby
    // WriteLog("Wysylam pojazd");
    DaneRozkaz r;
    r.iSygn = 'EU07';
    r.iComm = 7; // 7 - dane pojazdu
    int i = 32, j = t->asName.Length();
    r.iPar[0] = i; // ilo�� danych liczbowych
    r.fPar[1] = Global::fTimeAngleDeg / 360.0; // aktualny czas (1.0=doba)
    r.fPar[2] = t->DynamicObject->MoverParameters->Loc.X; // pozycja X
    r.fPar[3] = t->DynamicObject->MoverParameters->Loc.Y; // pozycja Y
    r.fPar[4] = t->DynamicObject->MoverParameters->Loc.Z; // pozycja Z
    r.fPar[5] = t->DynamicObject->MoverParameters->V; // pr�dko�� ruchu X
    r.fPar[6] = t->DynamicObject->MoverParameters->nrot * M_PI *
                t->DynamicObject->MoverParameters->WheelDiameter; // pr�dko�� obrotowa k�
    r.fPar[7] = 0; // pr�dko�� ruchu Z
    r.fPar[8] = t->DynamicObject->MoverParameters->AccS; // przyspieszenie X
    r.fPar[9] = t->DynamicObject->MoverParameters->AccN; // przyspieszenie Y //na razie nie
    r.fPar[10] = t->DynamicObject->MoverParameters->AccV; // przyspieszenie Z
    r.fPar[11] = t->DynamicObject->MoverParameters->DistCounter; // przejechana odleg�o�� w km
    r.fPar[12] = t->DynamicObject->MoverParameters->PipePress; // ci�nienie w PG
    r.fPar[13] = t->DynamicObject->MoverParameters->ScndPipePress; // ci�nienie w PZ
    r.fPar[14] = t->DynamicObject->MoverParameters->BrakePress; // ci�nienie w CH
    r.fPar[15] = t->DynamicObject->MoverParameters->Compressor; // ci�nienie w ZG
    r.fPar[16] = t->DynamicObject->MoverParameters->Itot; // Pr�d ca�kowity
    r.iPar[17] = t->DynamicObject->MoverParameters->MainCtrlPos; // Pozycja NJ
    r.iPar[18] = t->DynamicObject->MoverParameters->ScndCtrlPos; // Pozycja NB
    r.iPar[19] = t->DynamicObject->MoverParameters->MainCtrlActualPos; // Pozycja jezdna
    r.iPar[20] = t->DynamicObject->MoverParameters->ScndCtrlActualPos; // Pozycja bocznikowania
    r.iPar[21] = t->DynamicObject->MoverParameters->ScndCtrlActualPos; // Pozycja bocznikowania
    r.iPar[22] = t->DynamicObject->MoverParameters->ResistorsFlag * 1 +
                 t->DynamicObject->MoverParameters->ConverterFlag * 2 +
                 +t->DynamicObject->MoverParameters->CompressorFlag * 4 +
                 t->DynamicObject->MoverParameters->Mains * 8 +
                 +t->DynamicObject->MoverParameters->DoorLeftOpened * 16 +
                 t->DynamicObject->MoverParameters->DoorRightOpened * 32 +
                 +t->DynamicObject->MoverParameters->FuseFlag * 64 +
                 t->DynamicObject->MoverParameters->DepartureSignal * 128;
    // WriteLog("Zapisalem stare");
    // WriteLog("Mam patykow "+IntToStr(t->DynamicObject->iAnimType[ANIM_PANTS]));
    for (int p = 0; p < 4; p++)
    {
        //   WriteLog("Probuje pant "+IntToStr(p));
        if (p < t->DynamicObject->iAnimType[ANIM_PANTS])
        {
            r.fPar[23 + p] = t->DynamicObject->pants[p].fParamPants->PantWys; // stan pantograf�w 4
            //     WriteLog("Zapisalem pant "+IntToStr(p));
        }
        else
        {
            r.fPar[23 + p] = -2;
            //     WriteLog("Nie mam pant "+IntToStr(p));
        }
    }
    // WriteLog("Zapisalem pantografy");
    for (int p = 0; p < 3; p++)
        r.fPar[27 + p] =
            t->DynamicObject->MoverParameters->ShowCurrent(p + 1); // amperomierze kolejnych grup
    // WriteLog("zapisalem prady");
    r.iPar[30] = t->DynamicObject->MoverParameters->WarningSignal; // trabienie
    r.fPar[31] = t->DynamicObject->MoverParameters->RunningTraction.TractionVoltage; // napiecie WN
    // WriteLog("Parametry gotowe");
    i <<= 2; // ilo�� bajt�w
    r.cString[i] = char(j); // na ko�cu nazwa, �eby jako� zidentyfikowa�
    strcpy(r.cString + i + 1, t->asName.c_str()); // zako�czony zerem
    COPYDATASTRUCT cData;
    cData.dwData = 'EU07'; // sygnatura
    cData.cbData = 10 + i + j; // 8+licznik i zero ko�cz�ce
    cData.lpData = &r;
    // WriteLog("Ramka gotowa");
    Navigate("TEU07SRK", WM_COPYDATA, (WPARAM)Global::hWnd, (LPARAM)&cData);
    // WriteLog("Ramka poszla!");
	CommLog(AnsiString(Now()) + " " + IntToStr(r.iComm) + " " + t->asName + " sent");
};
//
void TGround::WyslijObsadzone()
{   // wys�anie informacji o poje�dzie
	DaneRozkaz2 r;
	r.iSygn = 'EU07';
	r.iComm = 12;   // kod 12
	for (int i; i<1984; i++) r.cString[i] = 0;

	int i = 0;
	for (TGroundNode *Current = nRootDynamic; Current; Current = Current->nNext)
		if (Current->DynamicObject->Mechanik)
		{
			strcpy(r.cString + 64 * i, Current->DynamicObject->asName.c_str());
			r.fPar[16 * i + 4] = Current->DynamicObject->GetPosition().x;
			r.fPar[16 * i + 5] = Current->DynamicObject->GetPosition().y;
			r.fPar[16 * i + 6] = Current->DynamicObject->GetPosition().z;
			r.iPar[16 * i + 7] = Current->DynamicObject->Mechanik->GetAction();
			strcpy(r.cString + 64 * i + 32, Current->DynamicObject->GetTrack()->IsolatedName().c_str());
			strcpy(r.cString + 64 * i + 48, Current->DynamicObject->Mechanik->Timetable()->TrainName.c_str());
			i++;
			if (i>30) break;
		}
	while (i <= 30)
	{
		strcpy(r.cString + 64 * i, AnsiString("none").c_str());
		r.fPar[16 * i + 4] = 1;
		r.fPar[16 * i + 5] = 2;
		r.fPar[16 * i + 6] = 3;
		r.iPar[16 * i + 7] = 0;
		strcpy(r.cString + 64 * i + 32, AnsiString("none").c_str());
		strcpy(r.cString + 64 * i + 48, AnsiString("none").c_str());
		i++;
	}

	COPYDATASTRUCT cData;
	cData.dwData = 'EU07';     // sygnatura
	cData.cbData = 8 + 1984; // 8+licznik i zero ko�cz�ce
	cData.lpData = &r;
	// WriteLog("Ramka gotowa");
	Navigate("TEU07SRK", WM_COPYDATA, (WPARAM)Global::hWnd, (LPARAM)&cData);
	CommLog(AnsiString(Now()) + " " + IntToStr(r.iComm) + " obsadzone" + " sent");
}

//--------------------------------
void TGround::WyslijParam(int nr, int fl)
{ // wys�anie parametr�w symulacji w ramce (nr) z flagami (fl)
    DaneRozkaz r;
    r.iSygn = 'EU07';
    r.iComm = nr; // zwykle 5
    r.iPar[0] = fl; // flagi istotno�ci kolejnych parametr�w
    int i = 0; // domy�lnie brak danych
    switch (nr)
    { // mo�na tym przesy�a� r�ne zestawy parametr�w
    case 5: // czas i pauza
        r.fPar[1] = Global::fTimeAngleDeg / 360.0; // aktualny czas (1.0=doba)
        r.iPar[2] = Global::iPause; // stan zapauzowania
        i = 8; // dwa parametry po 4 bajty ka�dy
        break;
    }
    COPYDATASTRUCT cData;
    cData.dwData = 'EU07'; // sygnatura
    cData.cbData = 12 + i; // 12+rozmiar danych
    cData.lpData = &r;
    Navigate("TEU07SRK", WM_COPYDATA, (WPARAM)Global::hWnd, (LPARAM)&cData);
};
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void TGround::RadioStop(vector3 pPosition)
{ // zatrzymanie poci�g�w w okolicy
    TGroundNode *node;
    TSubRect *tmp;
    int c = GetColFromX(pPosition.x);
    int r = GetRowFromZ(pPosition.z);
    int i, j;
    int n = 2 * iNumSubRects; // przegl�danie czo�gowe okolicznych tor�w w kwadracie 4km�4km
    for (j = r - n; j <= r + n; j++)
        for (i = c - n; i <= c + n; i++)
            if ((tmp = FastGetSubRect(i, j)) != NULL)
                for (node = tmp->nRootNode; node != NULL; node = node->nNext2)
                    if (node->iType == TP_TRACK)
                        node->pTrack->RadioStop(); // przekazanie do ka�dego toru w ka�dym segmencie
};

TDynamicObject * TGround::DynamicNearest(vector3 pPosition, double distance, bool mech)
{ // wyszukanie pojazdu najbli�szego wzgl�dem (pPosition)
    TGroundNode *node;
    TSubRect *tmp;
    TDynamicObject *dyn = NULL;
    int c = GetColFromX(pPosition.x);
    int r = GetRowFromZ(pPosition.z);
    int i, j, k;
    double sqm = distance * distance, sqd; // maksymalny promien poszukiwa� do kwadratu
    for (j = r - 1; j <= r + 1; j++) // plus dwa zewn�trzne sektory, ��cznie 9
        for (i = c - 1; i <= c + 1; i++)
            if ((tmp = FastGetSubRect(i, j)) != NULL)
                for (node = tmp->nRootNode; node; node = node->nNext2) // nast�pny z sektora
                    if (node->iType == TP_TRACK) // Ra: przebudowa� na u�ycie tabeli tor�w?
                        for (k = 0; k < node->pTrack->iNumDynamics; k++)
                            if (mech ? (node->pTrack->Dynamics[k]->Mechanik != NULL) :
                                       true) // czy ma mie� obsad�
                                if ((sqd = SquareMagnitude(
                                         node->pTrack->Dynamics[k]->GetPosition() - pPosition)) <
                                    sqm)
                                {
                                    sqm = sqd; // nowa odleg�o��
                                    dyn = node->pTrack->Dynamics[k]; // nowy lider
                                }
    return dyn;
};
TDynamicObject * TGround::CouplerNearest(vector3 pPosition, double distance, bool mech)
{ // wyszukanie pojazdu, kt�rego sprz�g jest najbli�ej wzgl�dem (pPosition)
    TGroundNode *node;
    TSubRect *tmp;
    TDynamicObject *dyn = NULL;
    int c = GetColFromX(pPosition.x);
    int r = GetRowFromZ(pPosition.z);
    int i, j, k;
    double sqm = distance * distance, sqd; // maksymalny promien poszukiwa� do kwadratu
    for (j = r - 1; j <= r + 1; j++) // plus dwa zewn�trzne sektory, ��cznie 9
        for (i = c - 1; i <= c + 1; i++)
            if ((tmp = FastGetSubRect(i, j)) != NULL)
                for (node = tmp->nRootNode; node; node = node->nNext2) // nast�pny z sektora
                    if (node->iType == TP_TRACK) // Ra: przebudowa� na u�ycie tabeli tor�w?
                        for (k = 0; k < node->pTrack->iNumDynamics; k++)
                            if (mech ? (node->pTrack->Dynamics[k]->Mechanik != NULL) :
                                       true) // czy ma mie� obsad�
                            {
                                if ((sqd = SquareMagnitude(
                                         node->pTrack->Dynamics[k]->HeadPosition() - pPosition)) <
                                    sqm)
                                {
                                    sqm = sqd; // nowa odleg�o��
                                    dyn = node->pTrack->Dynamics[k]; // nowy lider
                                }
                                if ((sqd = SquareMagnitude(
                                         node->pTrack->Dynamics[k]->RearPosition() - pPosition)) <
                                    sqm)
                                {
                                    sqm = sqd; // nowa odleg�o��
                                    dyn = node->pTrack->Dynamics[k]; // nowy lider
                                }
                            }
    return dyn;
};
//---------------------------------------------------------------------------
void TGround::DynamicRemove(TDynamicObject *dyn)
{ // Ra: usuni�cie pojazd�w ze scenerii (gdy dojad� na koniec i nie sa potrzebne)
    TDynamicObject *d = dyn->Prev();
    if (d) // je�li co� jest z przodu
        DynamicRemove(d); // zaczynamy od tego z przodu
    else
    { // je�li mamy ju� tego na pocz�tku
        TGroundNode **n, *node;
        d = dyn; // od pierwszego
        while (d)
        {
            if (d->MyTrack)
                d->MyTrack->RemoveDynamicObject(d); // usuni�cie z toru o ile nie usuni�ty
            n = &nRootDynamic; // lista pojazd�w od pocz�tku
            // node=NULL; //nie znalezione
            while (*n ? (*n)->DynamicObject != d : false)
            { // usuwanie z listy pojazd�w
                n = &((*n)->nNext); // sprawdzenie kolejnego pojazdu na li�cie
            }
            if ((*n)->DynamicObject == d)
            { // je�li znaleziony
                node = (*n); // zapami�tanie w�z�a, aby go usun��
                (*n) = node->nNext; // pomini�cie na li�cie
                Global::TrainDelete(d);
                d = d->Next(); // przej�cie do kolejnego pojazdu, p�ki jeszcze jest
                delete node; // usuwanie fizyczne z pami�ci
            }
            else
                d = NULL; // co� nie tak!
        }
    }
};

//---------------------------------------------------------------------------
void TGround::TerrainRead(const AnsiString &f){
    // Ra: wczytanie tr�jk�t�w terenu z pliku E3D
};

//---------------------------------------------------------------------------
void TGround::TerrainWrite()
{ // Ra: zapisywanie tr�jk�t�w terenu do pliku E3D
    if (Global::pTerrainCompact->TerrainLoaded())
        return; // je�li zosta�o wczytane, to nie ma co dalej robi�
    if (Global::asTerrainModel.IsEmpty())
        return;
    // Tr�jk�ty s� zapisywane kwadratami kilometrowymi.
    // Kwadrat 500500 jest na �rodku (od 0.0 do 1000.0 na OX oraz OZ).
    // Ewentualnie w numerowaniu kwadrat�w uwzgl�dnic wpis //$g.
    // Tr�jk�ty s� grupowane w submodele wg tekstury.
    // Triangle_strip oraz triangle_fan s� rozpisywane na pojedyncze tr�jk�ty,
    // chyba �e dla danej tekstury wychodzi tylko jeden submodel.
    TModel3d *m = new TModel3d(); // wirtualny model roboczy z oddzielnymi submodelami
    TSubModel *sk; // wska�nik roboczy na submodel kwadratu
    TSubModel *st; // wska�nik roboczy na submodel tekstury
    // Zliczamy kwadraty z tr�jk�tami, ilo�� tekstur oraz wierzcho�k�w.
    // Ilo�� kwadrat�w i ilo�� tekstur okre�li ilo�� submodeli.
    // int sub=0; //ca�kowita ilo�� submodeli
    // int ver=0; //ca�kowita ilo�� wierzcho�k�w
    int i, j, k; // indeksy w p�tli
    TGroundNode *Current;
    float8 *ver; // tr�jk�ty
    TSubModel::iInstance = 0; // pozycja w tabeli wierzcho�k�w liczona narastaj�co
    for (i = 0; i < iNumRects; ++i) // p�tla po wszystkich kwadratach kilometrowych
        for (j = 0; j < iNumRects; ++j)
            if (Rects[i][j].iNodeCount)
            { // o ile s� jakie� tr�jk�ty w �rodku
                sk = new TSubModel(); // nowy submodel dla kawadratu
                // numer kwadratu XXXZZZ, przy czym X jest ujemne - XXX ro�nie na wsch�d, ZZZ ro�nie
                // na p�noc
                sk->NameSet(AnsiString(1000 * (500 + i - iNumRects / 2) + (500 + j - iNumRects / 2))
                                .c_str()); // nazwa=numer kwadratu
                m->AddTo(NULL, sk); // dodanie submodelu dla kwadratu
                for (Current = Rects[i][j].nRootNode; Current; Current = Current->nNext2)
                    if (Current->TextureID)
                        switch (Current->iType)
                        { // p�tla po tr�jk�tach - zliczanie wierzcho�k�w, dodaje submodel dla
                        // ka�dej tekstury
                        case GL_TRIANGLES:
                            Current->iVboPtr = sk->TriangleAdd(
                                m, Current->TextureID,
                                Current->iNumVerts); // zwi�kszenie ilo�ci tr�jk�t�w w submodelu
                            m->iNumVerts +=
                                Current->iNumVerts; // zwi�kszenie ca�kowitej ilo�ci wierzcho�k�w
                            break;
                        case GL_TRIANGLE_STRIP: // na razie nie, bo trzeba przerabia� na pojedyncze
                            // tr�jk�ty
                            break;
                        case GL_TRIANGLE_FAN: // na razie nie, bo trzeba przerabia� na pojedyncze
                            // tr�jk�ty
                            break;
                        }
                for (Current = Rects[i][j].nRootNode; Current; Current = Current->nNext2)
                    if (Current->TextureID)
                        switch (Current->iType)
                        { // p�tla po tr�jk�tach - dopisywanie wierzcho�k�w
                        case GL_TRIANGLES:
                            // ver=sk->TrianglePtr(TTexturesManager::GetName(Current->TextureID).c_str(),Current->iNumVerts);
                            // //wska�nik na pocz�tek
                            ver = sk->TrianglePtr(Current->TextureID, Current->iVboPtr,
                                                  Current->Ambient, Current->Diffuse,
                                                  Current->Specular); // wska�nik na pocz�tek
                            // WriteLog("Zapis "+AnsiString(Current->iNumVerts)+" tr�jk�t�w w
                            // ("+AnsiString(i)+","+AnsiString(j)+") od
                            // "+AnsiString(Current->iVboPtr)+" dla
                            // "+AnsiString(Current->TextureID));
                            Current->iVboPtr = -1; // bo to by�o tymczasowo u�ywane
                            for (k = 0; k < Current->iNumVerts; ++k)
                            { // przepisanie wsp�rz�dnych
                                ver[k].Point.x = Current->Vertices[k].Point.x;
                                ver[k].Point.y = Current->Vertices[k].Point.y;
                                ver[k].Point.z = Current->Vertices[k].Point.z;
                                ver[k].Normal.x = Current->Vertices[k].Normal.x;
                                ver[k].Normal.y = Current->Vertices[k].Normal.y;
                                ver[k].Normal.z = Current->Vertices[k].Normal.z;
                                ver[k].tu = Current->Vertices[k].tu;
                                ver[k].tv = Current->Vertices[k].tv;
                            }
                            break;
                        case GL_TRIANGLE_STRIP: // na razie nie, bo trzeba przerabia� na pojedyncze
                            // tr�jk�ty
                            break;
                        case GL_TRIANGLE_FAN: // na razie nie, bo trzeba przerabia� na pojedyncze
                            // tr�jk�ty
                            break;
                        }
            }
    m->SaveToBinFile(AnsiString("models\\" + Global::asTerrainModel).c_str());
};
//---------------------------------------------------------------------------

void TGround::TrackBusyList()
{ // wys�anie informacji o wszystkich zaj�tych odcinkach
    TGroundNode *Current;
    TTrack *Track;
    AnsiString name;
    for (Current = nRootOfType[TP_TRACK]; Current; Current = Current->nNext)
        if (!Current->asName.IsEmpty()) // musi by� nazwa
            if (Current->pTrack->iNumDynamics) // osi to chyba nie ma jak policzy�
                WyslijString(Current->asName, 8); // zaj�ty
};
//---------------------------------------------------------------------------

void TGround::IsolatedBusyList()
{ // wys�anie informacji o wszystkich odcinkach izolowanych
    TIsolated *Current;
    for (Current = TIsolated::Root(); Current; Current = Current->Next())
        if (Current->Busy()) // sprawd� zaj�to��
            WyslijString(Current->asName, 11); // zaj�ty
        else
            WyslijString(Current->asName, 10); // wolny
    WyslijString("none", 10); // informacja o ko�cu listy
};
//---------------------------------------------------------------------------

void TGround::IsolatedBusy(const AnsiString t)
{ // wys�anie informacji o odcinku izolowanym (t)
    // Ra 2014-06: do wyszukania u�y� drzewka nazw
    TIsolated *Current;
    for (Current = TIsolated::Root(); Current; Current = Current->Next())
        if (Current->asName == t) // wyszukiwanie odcinka o nazwie (t)
            if (Current->Busy()) // sprawd� zajeto��
            {
                WyslijString(Current->asName, 11); // zaj�ty
                return; // nie sprawdzaj dalszych
            }
    WyslijString(t, 10); // wolny
};
//---------------------------------------------------------------------------

void TGround::Silence(vector3 gdzie)
{ // wyciszenie wszystkiego w sektorach przed przeniesieniem kamery z (gdzie)
    int tr, tc;
    TGroundNode *node;
    int n = 2 * iNumSubRects; //(2*==2km) promie� wy�wietlanej mapy w sektorach
    int c = GetColFromX(gdzie.x); // sektory wg dotychczasowej pozycji kamery
    int r = GetRowFromZ(gdzie.z);
    TSubRect *tmp;
    int i, j, k;
    // renderowanie czo�gowe dla obiekt�w aktywnych a niewidocznych
    for (j = r - n; j <= r + n; j++)
        for (i = c - n; i <= c + n; i++)
            if ((tmp = FastGetSubRect(i, j)) != NULL)
            { // tylko d�wi�ki interesuj�
                for (node = tmp->nRenderHidden; node; node = node->nNext3)
                    node->RenderHidden();
                tmp->RenderSounds(); // d�wi�ki pojazd�w by si� przyda�o wy��czy�
            }
};
//---------------------------------------------------------------------------
