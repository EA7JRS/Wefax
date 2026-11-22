# Licencias de Wefax

Wefax · © 2026 Manuel - EA7JRS

## El programa

Se distribuye bajo la **GNU General Public License, versión 3 o posterior** (`GPL-3.0-or-later`). El texto está en [LICENSE](LICENSE).

    Este programa es software libre: puede redistribuirlo y modificarlo bajo los
    términos de la GNU General Public License publicada por la Free Software
    Foundation, en su versión 3 o, a su elección, cualquier versión posterior.

    Este programa se distribuye con la esperanza de que sea útil, pero SIN
    NINGUNA GARANTÍA; ni siquiera la garantía implícita de COMERCIALIZACIÓN o
    de ADECUACIÓN A UN FIN DETERMINADO. Véase la licencia para más detalles.

## Permiso adicional: API de SDRplay

Para recibir con equipos SDRplay, el programa se enlaza con la API de SDRplay, que es una biblioteca privativa. Conforme a la sección 7 de la GPL versión 3 se concede este permiso adicional:

    Additional permission under GNU GPL version 3 section 7

    If you modify this Program, or any covered work, by linking or combining
    it with the SDRplay API library (or a modified version of that library),
    containing parts covered by the terms of the SDRplay API licence, the
    licensors of this Program grant you additional permission to convey the
    resulting work. Corresponding Source for a non-source form of such a
    combination shall include the source code for the parts of this Program,
    but not for the SDRplay API library.

**Aviso para quien distribuya ejecutables.** Este proyecto se distribuye en código fuente. El soporte de RTL-SDR se enlaza con librtlsdr, que es GPL y de otros autores, y este permiso no alcanza a esa biblioteca. Quien quiera distribuir un ejecutable compilado a la vez con librtlsdr y con la API de SDRplay debe valorar por su cuenta si las dos licencias lo permiten.

## Lo que procede de otros

| Parte | De dónde | Licencia |
| :--- | :--- | :--- |
| `receiver/sdrplay_source.c` | La secuencia de llamadas a la API de SDRplay es la del receptor de [Navtex](https://github.com/EA7JRS/Navtex), que procede de Navtex de bartelvdh | MIT; su aviso y el texto de la licencia, en [LICENSE-MIT-original](LICENSE-MIT-original) |
| `receiver/rtl_source.c`, `receiver/file_source.c`, `receiver/iq_source.h` | Del fork EA7JRS/Navtex, del mismo autor que este proyecto | GPL-3.0-or-later |
| Descodificador (`receiver/wefax_decoder.c`) | Código nuevo. Los tonos y el pulso de enfasado son los del Manual n.º 386 de la OMM; se consultó cómo los trata fldigi (W1HKJ y otros, GPL-3) | GPL-3.0-or-later |

## Bibliotecas con las que se enlaza

zlib (licencia zlib), cJSON (MIT), SQLite (dominio público), librtlsdr (GPL-2.0-or-later, opcional) y la API de SDRplay (privativa, opcional).
