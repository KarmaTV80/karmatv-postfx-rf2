# KarmaTV PostFX for rFactor2

[English](README.md) · [Italiano](README.it.md) · **Español** · [Français](README.fr.md) · [Deutsch](README.de.md) · [Português](README.pt.md)

Efectos gráficos para rFactor 2 con un menú en el juego: perfiles listos, ajustes avanzados, automatismos guiados por la telemetría (lluvia, repeticiones, cámara) y actualizaciones automáticas. Es un add-on de [ReShade](https://reshade.me); el instalador deja ReShade listo. El menú está en inglés, italiano, español, francés, alemán y portugués.

## Instalación

1. Descarga `KarmaTV-PostFX-Setup-x.y.z.exe` de la [última versión](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Cierra rFactor 2 y ejecuta el instalador: encuentra el juego en tus bibliotecas de Steam.
3. Inicia rF2, sal a pista y pulsa **Supr** para abrir el menú.

Windows puede mostrar "Windows protegió su PC" porque el instalador no tiene firma digital: elige **Más información** y luego **Ejecutar de todas formas**.

Los automatismos necesitan [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), activado en las opciones de Plugins de rF2. Los efectos funcionan sin él.

Para desinstalar: **Configuración de Windows > Aplicaciones > Aplicaciones instaladas > KarmaTV PostFX for rFactor2**. Tus perfiles se conservan, salvo que elijas borrarlos.

## Perfiles

| Perfil | Para qué |
|---|---|
| rF2 Original | Sin efectos: los gráficos del juego |
| Karma FX | Conducción en monitor |
| Replay Karma FX | Repeticiones y realización: profundidad de campo, motion blur. Se activa solo en las repeticiones |
| VR Karma FX | Visor, con efectos adecuados para VR |

Los perfiles fijos se pueden modificar con el botón **Avanzado**, pero los cambios solo se guardan como perfil nuevo. **Usar al iniciar** elige el perfil que se carga al abrir el juego.

## Teclas

| Tecla | Acción |
|---|---|
| Supr | Abre y cierra el menú |
| + | Efectos sí/no |
| , y . | Perfil anterior / siguiente |
| - | Comparación A/B (a la izquierda la imagen original) |

Se cambian desde el menú con **Personalizar teclas**, también con los botones del volante. El idioma se cambia con el botón de la cabecera del menú.

## Actualizaciones

Al iniciar el juego, el add-on comprueba si hay una versión nueva en GitHub. Si la hay, el menú muestra **Actualizar**: el instalador se descarga, se comprueba con su huella SHA-256 y se instala al cerrar rFactor 2. La comprobación solo envía a GitHub el nombre y la versión del programa.

Para compilar o ayudar con las traducciones, mira la sección *Development* del [README en inglés](README.md#development).

## Licencia

Gratuito, todos los derechos reservados: consulta [LICENSE.txt](LICENSE.txt). ReShade se distribuye con licencia BSD-3-Clause y la fuente Barlow Condensed con SIL Open Font License.
