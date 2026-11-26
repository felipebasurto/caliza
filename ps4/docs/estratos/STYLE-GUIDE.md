# Caliza PS4 · Estratos (AI-assisted)

**Diseño de interfaz original para televisión.** Una consola óptica imaginaria fabricada con basalto, láminas de cuarzo y luz fría. La referencia PS2UI aporta oscuridad, profundidad y contraste. El motivo propio son las capas minerales cortadas, con una única veta iluminada. El motivo vive en el arranque o en un espacio libre. En la biblioteca manda la colección.

## Referencias de diseño

- **PS2UI** distingue el aspecto de la pantalla de los materiales físicos de la consola. En Estratos, el arranque y las transiciones pueden insinuar profundidad. Las listas, controles y textos se apoyan en superficies mates y conservan el foco visible. [Kit PS2UI](https://github.com/t1mdurden/ps2ui)
- **La especificación de PS2UI** documenta fondos por pantalla, jerarquía tipográfica y transiciones analizadas fotograma a fotograma. Aplica esa separación entre fondo, texto, foco y movimiento al formato 16:9 de Caliza. Da una respuesta inmediata al mando, deja el movimiento ambiental en segundo plano y permite desactivarlo. [Investigación y especificación de PS2UI](https://github.com/t1mdurden/ps2ui/blob/main/research/DESIGN.md)
- **OSD-XMB** documenta el descubrimiento de juegos desde varios dispositivos y el acceso a ajustes desde el juego seleccionado. En Caliza, muestra primero las unidades disponibles y su estado. Sugiere carpetas de juegos reconocibles cuando existan y permite elegir cualquier otra carpeta compatible con el mando. Conserva los ajustes por juego en su vista de detalle. La compatibilidad con un disco se comprueba en PS4. [OSD-XMB](https://github.com/HiroTex/OSD-XMB)

Usa estas referencias para estudiar cómo responde una interfaz de consola. Implementa las pantallas con la composición de Estratos, la cuadrícula de la biblioteca, el selector de ubicaciones y el logo aprobado. El código, los recursos y las composiciones de los proyectos de referencia quedan fuera de la nueva interfaz.

## Logo aprobado

Usa la esquina superior izquierda de [01-home.png](01-home.png) como referencia maestra. Mantén las tres láminas diagonales y escalonadas, la silueta azul hielo y el wordmark CALIZA. Dibuja un vector fiel a ese diseño, con la misma silueta en monocromo y a tamaño pequeño. Usa una versión consistente en el icono, el arranque, la interfaz y el panel web.

## Selector de carpetas

Abre en ubicaciones reconocibles, con un icono, nombre de unidad y estado. X abre una carpeta y Círculo vuelve al nivel anterior. Mantén «Usar esta carpeta» como acción explícita y conserva el foco cuando el jugador vuelve. Muestra la ruta con migas legibles, un estado breve durante el escaneo y una acción «Volver a conectar» o «Cambiar ubicación» si la unidad desaparece. Da prioridad al nombre del disco y de la carpeta sobre las rutas técnicas.

## Paleta

| Papel | Valor |
| --- | --- |
| Basalto, fondo | `#080B12` |
| Grafito, superficie | `#141B28` |
| Índigo, profundidad | `#202957` |
| Hielo, foco activo | `#A5E7FF` |
| Blanco frío, texto principal | `#EEF4FF` |
| Gris azulado, texto secundario | `#B4C3D8` |
| Ámbar, recuperación | `#FFD092` |

Mantén regularidad de espaciado y usa texturas muy suaves fuera de los controles. Usa fondo opaco o suficientemente estable detrás de texto, incluyendo la pausa sobre el juego. Evita scanlines sobre información esencial. Los valores de contraste se miden con los colores compuestos que llegan al televisor, también encima de arte. El color nunca es la única señal de foco ni de error.

## Lectura y navegación desde el sofá

- Usa una sans humanista para texto y una geométrica moderada para la marca. A 1080p, usa 26 px de cuerpo visible como mínimo de referencia en información funcional. Permite escalar hasta el 200 % con reflujo y desplazamiento en un solo eje. Ajusta en 720p y comprueba la lectura en TV. [XAG 101](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/101)
- Apunta a 4,5:1 de contraste para texto y elementos funcionales de tamaño normal, 3:1 para elementos grandes y 7:1 en el modo de alto contraste. Combina un contorno de foco claramente visible con forma o énfasis. [XAG 102](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/102)
- Usa una zona interior de diseño de aproximadamente 5 % por lado como punto de partida propio y verifica el encuadre real. Mantén los avisos de mando en una franja estable y despejada. Alinea el orden del foco con el orden visual y recupera el foco anterior al cerrar hojas, diálogos y vistas de detalle. [XAG 112](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/112)
- Prueba D-pad y stick, incluida repetición al mantener, cuadrícula en sus bordes, listas vacías, retorno desde un diálogo y desconexión. Haz que el estado y el siguiente paso sean claros cuando falte un recurso, el disco esté ocupado o una operación falle.
- Permite saltar el arranque y desactivar animación decorativa, paralaje y destellos. Foco inmediato y transiciones breves, sin reordenar los elementos mientras el jugador los recorre. [XAG 117](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117)

## Pantallas de referencia

| Archivo | Qué se toma de la imagen |
| --- | --- |
| `01-home.png` | Jerarquía del inicio y estratos como motivo de marca |
| `02-library.png` | Cuadrícula, filtros y foco regular, sin una carátula dominante |
| `03-game-details.png` | Acción de jugar en primer plano y acceso a opciones en contexto |
| `04-first-run.png` | Recuperación guiada al faltar el BIOS |
| `05-in-game-overlay.png` | Pausa legible con el juego aún visible |
| `06-memory-cards.png` | Ranuras y lista de partidas fáciles de distinguir |
| `07-settings.png` | Categorías a la izquierda y valores claros a la derecha |
| `08-controls.png` | Relación entre acción y botón, remapeo legible |
| `09-accessibility.png` | Escala, contraste y movimiento con previsualización |
| `10-diagnostics.png` | Estado del sistema, logs y acceso al panel |

Los renders son imágenes conceptuales generadas, con juegos y valores ficticios. Los textos y datos del producto deben salir del sistema implementado. El símbolo final deriva del logo aprobado del boceto de inicio. Una captura de concepto no sustituye una captura del PKG en una PS4.
