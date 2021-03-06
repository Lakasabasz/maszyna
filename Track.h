/*
This Source Code Form is subject to the
terms of the Mozilla Public License, v.
2.0. If a copy of the MPL was not
distributed with this file, You can
obtain one at
http://mozilla.org/MPL/2.0/.
*/

#ifndef TrackH
#define TrackH

#include "Segment.h"
#include "ResourceManager.h"
#include "opengl/glew.h"
#include <system.hpp>
#include "Classes.h"

class TEvent;

typedef enum
{
    tt_Unknown,
    tt_Normal,
    tt_Switch,
    tt_Table,
    tt_Cross,
    tt_Tributary
} TTrackType;
// McZapkie-100502
typedef enum
{
    e_unknown = -1,
    e_flat = 0,
    e_mountains,
    e_canyon,
    e_tunnel,
    e_bridge,
    e_bank
} TEnvironmentType;
// Ra: opracowa� alternatywny system cieni/�wiate� z definiowaniem koloru o�wietlenia w halach

class TTrack;
class TGroundNode;
class TSubRect;
class TTraction;

class TSwitchExtension
{ // dodatkowe dane do toru, kt�ry jest zwrotnic�
  public:
    TSwitchExtension(TTrack *owner, int what);
    ~TSwitchExtension();
    TSegment *Segments[6]; // dwa tory od punktu 1, pozosta�e dwa od 2? Ra 140101: 6 po��cze� dla
    // skrzy�owa�
    // TTrack *trNear[4]; //tory do��czone do punkt�w 1, 2, 3 i 4
    // dotychczasowe [2]+[2] wska�niki zamieni� na nowe [4]
    TTrack *pNexts[2]; // tory do��czone do punkt�w 2 i 4
    TTrack *pPrevs[2]; // tory do��czone do punkt�w 1 i 3
    int iNextDirection[2]; // to te� z [2]+[2] przerobi� na [4]
    int iPrevDirection[2];
    int CurrentIndex; // dla zwrotnicy
    double fOffset, fDesiredOffset; // aktualne i docelowe po�o�enie nap�du iglic
    double fOffsetSpeed; // pr�dko�� liniowa ruchu iglic
    double fOffsetDelay; // op�nienie ruchu drugiej iglicy wzgl�dem pierwszej
    union
    {
        struct
        { // zmienne potrzebne tylko dla zwrotnicy
            double fOffset1, fOffset2; // przesuni�cia iglic - 0=na wprost
            bool RightSwitch; // czy zwrotnica w prawo
        };
        struct
        { // zmienne potrzebne tylko dla obrotnicy/przesuwnicy
            TGroundNode *pMyNode; // dla obrotnicy do wt�rnego pod��czania tor�w
            // TAnimContainer *pAnim; //animator modelu dla obrotnicy
            TAnimModel *pModel; // na razie model
        };
        struct
        { // zmienne dla skrzy�owania
            vector3 *vPoints; // tablica wierzcho�k�w nawierzchni, generowana przez pobocze
            int iPoints; // liczba faktycznie u�ytych wierzcho�k�w nawierzchni
            bool bPoints; // czy utworzone?
            int iRoads; // ile dr�g si� spotyka?
        };
    };
    bool bMovement; // czy w trakcie animacji
    int iLeftVBO, iRightVBO; // indeksy iglic w VBO
    TSubRect *pOwner; // sektor, kt�remu trzeba zg�osi� animacj�
    TTrack *pNextAnim; // nast�pny tor do animowania
    TEvent *evPlus, *evMinus; // zdarzenia sygnalizacji rozprucia
    float fVelocity; // maksymalne ograniczenie pr�dko�ci (ustawianej eventem)
    vector3 vTrans; // docelowa translacja przesuwnicy
  private:
};

const int iMaxNumDynamics = 40; // McZapkie-100303

class TIsolated
{ // obiekt zbieraj�cy zaj�to�ci z kilku odcink�w
    int iAxles; // ilo�� osi na odcinkach obs�ugiwanych przez obiekt
    TIsolated *pNext; // odcinki izolowane s� trzymane w postaci listy jednikierunkowej
    static TIsolated *pRoot; // pocz�tek listy
  public:
    AnsiString asName; // nazwa obiektu, baza do nazw event�w
    TEvent *evBusy; // zdarzenie wyzwalane po zaj�ciu grupy
    TEvent *evFree; // zdarzenie wyzwalane po ca�kowitym zwolnieniu zaj�to�ci grupy
    TMemCell *pMemCell; // automatyczna kom�rka pami�ci, kt�ra wsp�pracuje z odcinkiem izolowanym
    TIsolated();
    TIsolated(const AnsiString &n, TIsolated *i);
    ~TIsolated();
    static TIsolated * Find(
        const AnsiString &n); // znalezienie obiektu albo utworzenie nowego
    void Modify(int i, TDynamicObject *o); // dodanie lub odj�cie osi
    bool Busy()
    {
        return (iAxles > 0);
    };
    static TIsolated * Root()
    {
        return (pRoot);
    };
    TIsolated * Next()
    {
        return (pNext);
    };
};

class TTrack : public Resource
{ // trajektoria ruchu - opakowanie
  private:
    TSwitchExtension *SwitchExtension; // dodatkowe dane do toru, kt�ry jest zwrotnic�
    TSegment *Segment;
    TTrack *trNext; // odcinek od strony punktu 2 - to powinno by� w segmencie
    TTrack *trPrev; // odcinek od strony punktu 1
    // McZapkie-070402: dodalem zmienne opisujace rozmiary tekstur
    GLuint TextureID1; // tekstura szyn albo nawierzchni
    GLuint TextureID2; // tekstura automatycznej podsypki albo pobocza
    float fTexLength; // d�ugo�� powtarzania tekstury w metrach
    float fTexRatio1; // proporcja rozmiar�w tekstury dla nawierzchni drogi
    float fTexRatio2; // proporcja rozmiar�w tekstury dla chodnika
    float fTexHeight1; // wysoko�� brzegu wzgl�dem trajektorii
    float fTexWidth; // szeroko�� boku
    float fTexSlope;
    double fRadiusTable[2]; // dwa promienie, drugi dla zwrotnicy
    int iTrapezoid; // 0-standard, 1-przechy�ka, 2-trapez, 3-oba
    GLuint DisplayListID;
    TIsolated *pIsolated; // obw�d izolowany obs�uguj�cy zaj�cia/zwolnienia grupy tor�w
    TGroundNode *
        pMyNode; // Ra: proteza, �eby tor zna� swoj� nazw� TODO: odziedziczy� TTrack z TGroundNode
  public:
    int iNumDynamics;
    TDynamicObject *Dynamics[iMaxNumDynamics];
    int iEvents; // Ra: flaga informuj�ca o obecno�ci event�w
    TEvent *evEventall0; // McZapkie-140302: wyzwalany gdy pojazd stoi
    TEvent *evEventall1;
    TEvent *evEventall2;
    TEvent *evEvent0; // McZapkie-280503: wyzwalany tylko gdy headdriver
    TEvent *evEvent1;
    TEvent *evEvent2;
    AnsiString asEventall0Name; // nazwy event�w
    AnsiString asEventall1Name;
    AnsiString asEventall2Name;
    AnsiString asEvent0Name;
    AnsiString asEvent1Name;
    AnsiString asEvent2Name;
    int iNextDirection; // 0:Point1, 1:Point2, 3:do odchylonego na zwrotnicy
    int iPrevDirection;
    TTrackType eType;
    int iCategoryFlag; // 0x100 - usuwanie pojaz�w
    float fTrackWidth; // szeroko�� w punkcie 1
    float fTrackWidth2; // szeroko�� w punkcie 2 (g��wnie drogi i rzeki)
    float fFriction; // wsp�czynnik tarcia
    float fSoundDistance;
    int iQualityFlag;
    int iDamageFlag;
    TEnvironmentType eEnvironment; // d�wi�k i o�wietlenie
    bool bVisible; // czy rysowany
    int iAction; // czy modyfikowany eventami (specjalna obs�uga przy skanowaniu)
    float fOverhead; // informacja o stanie sieci: 0-jazda bezpr�dowa, >0-z opuszczonym i
    // ograniczeniem pr�dko�ci
  private:
    double fVelocity; // pr�dko�� dla AI (powy�ej ro�nie prawdopowobie�stwo wykolejenia)
  public:
    // McZapkie-100502:
    double fTrackLength; // d�ugo�� z wpisu, nigdzie nie u�ywana
    double fRadius; // promie�, dla zwrotnicy kopiowany z tabeli
    bool ScannedFlag; // McZapkie: do zaznaczania kolorem tor�w skanowanych przez AI
    TTraction *hvOverhead; // drut zasilaj�cy do szybkiego znalezienia (nie u�ywany)
    TGroundNode *nFouling[2]; // wsp�rz�dne ukresu albo oporu koz�a
    TTrack *trColides; // tor kolizyjny, na kt�rym trzeba sprawdza� pojazdy pod k�tem zderzenia

    TTrack(TGroundNode *g);
    ~TTrack();
    void Init();
    static TTrack * Create400m(int what, double dx);
    TTrack * NullCreate(int dir);
    inline bool IsEmpty()
    {
        return (iNumDynamics <= 0);
    };
    void ConnectPrevPrev(TTrack *pNewPrev, int typ);
    void ConnectPrevNext(TTrack *pNewPrev, int typ);
    void ConnectNextPrev(TTrack *pNewNext, int typ);
    void ConnectNextNext(TTrack *pNewNext, int typ);
    inline double Length()
    {
        return Segment->GetLength();
    };
    inline TSegment * CurrentSegment()
    {
        return Segment;
    };
    inline TTrack * CurrentNext()
    {
        return (trNext);
    };
    inline TTrack * CurrentPrev()
    {
        return (trPrev);
    };
    TTrack * Neightbour(int s, double &d);
    bool SetConnections(int i);
    bool Switch(int i, double t = -1.0, double d = -1.0);
    bool SwitchForced(int i, TDynamicObject *o);
    int CrossSegment(int from, int into);
    inline int GetSwitchState()
    {
        return (SwitchExtension ? SwitchExtension->CurrentIndex : -1);
    };
    void Load(cParser *parser, vector3 pOrigin, AnsiString name);
    bool AssignEvents(TEvent *NewEvent0, TEvent *NewEvent1, TEvent *NewEvent2);
    bool AssignallEvents(TEvent *NewEvent0, TEvent *NewEvent1, TEvent *NewEvent2);
    bool AssignForcedEvents(TEvent *NewEventPlus, TEvent *NewEventMinus);
    bool CheckDynamicObject(TDynamicObject *Dynamic);
    bool AddDynamicObject(TDynamicObject *Dynamic);
    bool RemoveDynamicObject(TDynamicObject *Dynamic);
    void MoveMe(vector3 pPosition);

    void Release();
    void Compile(GLuint tex = 0);

    void Render(); // renderowanie z Display Lists
    int RaArrayPrepare(); // zliczanie rozmiaru dla VBO sektroa
    void RaArrayFill(CVertNormTex *Vert, const CVertNormTex *Start); // wype�nianie VBO
    void RaRenderVBO(int iPtr); // renderowanie z VBO sektora
    void RenderDyn(); // renderowanie nieprzezroczystych pojazd�w (oba tryby)
    void RenderDynAlpha(); // renderowanie przezroczystych pojazd�w (oba tryby)
    void RenderDynSounds(); // odtwarzanie d�wi�k�w pojazd�w jest niezale�ne od ich
    // wy�wietlania

    void RaOwnerSet(TSubRect *o)
    {
        if (SwitchExtension)
            SwitchExtension->pOwner = o;
    };
    bool InMovement(); // czy w trakcie animacji?
    void RaAssign(TGroundNode *gn, TAnimContainer *ac);
    void RaAssign(TGroundNode *gn, TAnimModel *am, TEvent *done, TEvent *joined);
    void RaAnimListAdd(TTrack *t);
    TTrack * RaAnimate();

    void RadioStop();
    void AxleCounter(int i, TDynamicObject *o)
    {
        if (pIsolated)
            pIsolated->Modify(i, o);
    }; // dodanie lub odj�cie osi
    AnsiString IsolatedName();
    bool IsolatedEventsAssign(TEvent *busy, TEvent *free);
    double WidthTotal();
    GLuint TextureGet(int i)
    {
        return i ? TextureID1 : TextureID2;
    };
    bool IsGroupable();
    int TestPoint(vector3 *Point);
    void MovedUp1(double dh);
    AnsiString NameGet();
    void VelocitySet(float v);
    float VelocityGet();
    void ConnectionsLog();

  private:
    void EnvironmentSet();
    void EnvironmentReset();
};

//---------------------------------------------------------------------------
#endif
