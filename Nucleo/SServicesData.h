/////////////////////////////////////////////////////////////
//
// Servicios de redhispana.org
// Est� prohibida la reproducci�n y el uso parcial o total
// del c�digo fuente de estos servicios sin previa
// autorizaci�n escrita del autor de estos servicios.
//
// Si usted viola esta licencia se emprender�n acciones legales.
//
// (C) RedHispana.Org 2009
//
// Archivo:     SServicesData.h
// Prop�sito:   Datos de los servicios para un usuario.
// Autores:     Alberto Alonso <rydencillo@gmail.com>
//

#pragma once

struct SServicesData
{
    SServicesData ()
        : bIdentified ( false ), ID ( 0 ), uiBadPasswords ( 0 )
    {
    }

    CString                     szLang;
    bool                        bIdentified;
    unsigned long long          ID;
    std::vector < unsigned long long >
                                vecChannelFounder;
    unsigned int                uiBadPasswords;

    struct
    {
        friend class CService;
    private:
        bool bCached;
        int iRank;
    } access;

    struct
    {
        friend class CChanserv;
    private:
        bool bCached;
        int iLevel;
    } chanAccess;
};
