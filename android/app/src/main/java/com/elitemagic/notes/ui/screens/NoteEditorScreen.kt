package com.elitemagic.notes.ui.screens

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.elitemagic.notes.model.DrawingPath
import com.elitemagic.notes.model.DrawingPoint
import com.elitemagic.notes.model.Note
import com.elitemagic.notes.watch.WatchBeaconManager
import kotlinx.coroutines.launch
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NoteEditorScreen(
    note: Note?,
    watchManager: WatchBeaconManager,
    onSave: (Note) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val gson = remember { Gson() }

    var title by remember { mutableStateOf(note?.title ?: "") }
    var content by remember { mutableStateOf(note?.content ?: "") }
    var isDrawingMode by remember { mutableStateOf(false) }

    // Load existing drawing paths from note if available
    var drawingPaths by remember {
        mutableStateOf<List<DrawingPath>>(
            note?.drawingData?.let { json ->
                try {
                    val type = object : TypeToken<List<DrawingPath>>() {}.type
                    gson.fromJson<List<DrawingPath>>(json, type)
                } catch (e: Exception) {
                    emptyList()
                }
            } ?: emptyList()
        )
    }
    var currentPath by remember { mutableStateOf<List<DrawingPoint>>(emptyList()) }

    // Drawing options
    var strokeWidth by remember { mutableStateOf(5f) }
    var selectedColor by remember { mutableStateOf(Color.Black) }

    val isScanning by watchManager.isScanning.collectAsState()
    val recognizedCard by watchManager.recognizedCard.collectAsState()
    val error by watchManager.error.collectAsState()
    val microphoneStartTime by watchManager.microphoneStartTime.collectAsState()

    var noteCreationTime by remember { mutableStateOf<Long?>(null) }
    var alreadyDrawnCards by remember { mutableStateOf<Set<String>>(emptySet()) }

    var hasAudioPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.RECORD_AUDIO
            ) == PackageManager.PERMISSION_GRANTED
        )
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        hasAudioPermission = isGranted
        if (isGranted) {
            watchManager.startScanning()
        }
    }

    // ESCRIBIR texto reconocido
    LaunchedEffect(recognizedCard) {
        recognizedCard?.let { text ->
            android.util.Log.d("NoteEditor", "Writing text: $text")

            // Guardar timestamp del micrófono
            microphoneStartTime?.let { timestamp ->
                noteCreationTime = timestamp
            }

            // Detectar si es una carta
            val detectedCards = detectCardsInText(text)

            if (detectedCards.isNotEmpty()) {
                // Es una carta - NO escribir, solo dibujar
                detectedCards.forEach { card ->
                    if (!alreadyDrawnCards.contains(card)) {
                        val cardPath = createCardDrawing(card)
                        drawingPaths = drawingPaths + cardPath
                        alreadyDrawnCards = alreadyDrawnCards + card
                        // Activar modo dibujo automáticamente
                        isDrawingMode = true
                    }
                }
            } else {
                // NO es carta - escribir normalmente
                content = if (content.isEmpty()) {
                    text
                } else {
                    "$content $text"
                }
            }

            watchManager.clearRecognizedCard()
        }
    }

    // Ya NO necesitamos detectar del contenido porque lo hacemos directamente arriba
    /*LaunchedEffect(content) {
        if (content.isNotEmpty()) {
            val detectedCards = detectCardsInText(content)
            detectedCards.forEach { card ->
                if (!alreadyDrawnCards.contains(card)) {
                    val cardPath = createCardDrawing(card)
                    drawingPaths = drawingPaths + cardPath
                    alreadyDrawnCards = alreadyDrawnCards + card
                }
            }
        }
    }*/

    Scaffold(
        topBar = {
            TopAppBar(
                title = { },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Volver")
                    }
                },
                actions = {
                    // Text icon button to activate microphone
                    IconButton(onClick = {
                        if (hasAudioPermission) {
                            if (isScanning) {
                                watchManager.stopScanning()
                            } else {
                                watchManager.startScanning()
                            }
                        } else {
                            permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                        }
                    }) {
                        Text(
                            text = "T",
                            fontSize = 24.sp,
                            fontWeight = FontWeight.Bold,
                            color = if (isScanning) Color.Red else Color.Gray
                        )
                    }

                    IconButton(onClick = { isDrawingMode = !isDrawingMode }) {
                        Icon(
                            Icons.Default.Edit,
                            contentDescription = "Dibujar",
                            tint = if (isDrawingMode) MaterialTheme.colorScheme.primary else Color.Gray
                        )
                    }

                    IconButton(onClick = {
                        // Serialize drawing paths to JSON
                        val drawingJson = if (drawingPaths.isNotEmpty()) {
                            gson.toJson(drawingPaths)
                        } else {
                            null
                        }

                        // Use microphone start time if available (for magic trick timing)
                        val creationTimestamp = noteCreationTime ?: note?.createdAt ?: System.currentTimeMillis()
                        val updateTimestamp = System.currentTimeMillis()

                        val noteToSave = note?.copy(
                            title = title,
                            content = content,
                            drawingData = drawingJson,
                            updatedAt = updateTimestamp
                        ) ?: Note(
                            title = title,
                            content = content,
                            drawingData = drawingJson,
                            createdAt = creationTimestamp,
                            updatedAt = updateTimestamp
                        )
                        onSave(noteToSave)
                        onBack()
                    }) {
                        Icon(Icons.Default.Check, contentDescription = "Guardar")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = Color.White
                )
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .background(Color.White)
        ) {
            // Title field
            TextField(
                value = title,
                onValueChange = { title = it },
                placeholder = {
                    Text(
                        "Título",
                        color = Color.LightGray,
                        fontSize = 20.sp
                    )
                },
                textStyle = LocalTextStyle.current.copy(
                    fontSize = 20.sp,
                    fontWeight = FontWeight.Normal
                ),
                colors = TextFieldDefaults.colors(
                    focusedContainerColor = Color.Transparent,
                    unfocusedContainerColor = Color.Transparent,
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent
                ),
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp)
            )

            // Mostrar fecha/hora actual de la nota
            val displayTimestamp = noteCreationTime ?: note?.createdAt ?: System.currentTimeMillis()
            val sdf = SimpleDateFormat("d 'de' MMMM HH:mm", Locale("es", "ES"))
            val dateStr = sdf.format(Date(displayTimestamp))
            val charCount = content.length

            Text(
                text = "$dateStr  |  $charCount caracteres",
                fontSize = 12.sp,
                color = Color.Gray,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
            )

            Divider(color = Color.LightGray, thickness = 0.5.dp)

            // Content area - Text OR Drawing (not together)
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(Color.White)
            ) {
                if (!isDrawingMode) {
                    // Text field when NOT in drawing mode
                    TextField(
                        value = content,
                        onValueChange = { content = it },
                        placeholder = {
                            Text(
                                "Empieza a escribir",
                                color = Color.LightGray
                            )
                        },
                        colors = TextFieldDefaults.colors(
                            focusedContainerColor = Color.Transparent,
                            unfocusedContainerColor = Color.Transparent,
                            focusedIndicatorColor = Color.Transparent,
                            unfocusedIndicatorColor = Color.Transparent
                        ),
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(horizontal = 16.dp)
                    )
                } else {
                    // Drawing canvas when in drawing mode - white background, no placeholder
                    DrawingCanvas(
                        paths = drawingPaths,
                        currentPath = currentPath,
                        currentColor = selectedColor,
                        currentStrokeWidth = strokeWidth,
                        onPathUpdate = { newPoint ->
                            currentPath = currentPath.toMutableList().apply { add(newPoint) }
                        },
                        onPathEnd = {
                            if (currentPath.isNotEmpty()) {
                                drawingPaths = drawingPaths + DrawingPath(
                                    points = currentPath,
                                    color = selectedColor.value.toLong(),
                                    strokeWidth = strokeWidth
                                )
                                currentPath = emptyList()
                            }
                        },
                        modifier = Modifier
                            .fillMaxSize()
                            .background(Color.White)
                            .padding(16.dp)
                    )
                }
            }

            // Drawing toolbar with color and stroke options (only show when drawing mode is active)
            if (isDrawingMode) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFFF5F5F5))
                        .padding(8.dp)
                ) {
                    // Color selector
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceEvenly
                    ) {
                        ColorButton(Color.Black, selectedColor == Color.Black) { selectedColor = Color.Black }
                        ColorButton(Color.Blue, selectedColor == Color.Blue) { selectedColor = Color.Blue }
                        ColorButton(Color.Red, selectedColor == Color.Red) { selectedColor = Color.Red }
                        ColorButton(Color.Green, selectedColor == Color.Green) { selectedColor = Color.Green }
                        ColorButton(Color(0xFFFFB300), selectedColor == Color(0xFFFFB300)) { selectedColor = Color(0xFFFFB300) }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    // Stroke width selector
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text("Grosor:", fontSize = 14.sp)
                        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            StrokeButton(3f, strokeWidth == 3f) { strokeWidth = 3f }
                            StrokeButton(5f, strokeWidth == 5f) { strokeWidth = 5f }
                            StrokeButton(8f, strokeWidth == 8f) { strokeWidth = 8f }
                            StrokeButton(12f, strokeWidth == 12f) { strokeWidth = 12f }
                        }

                        Row {
                            IconButton(onClick = {
                                if (drawingPaths.isNotEmpty()) {
                                    drawingPaths = drawingPaths.dropLast(1)
                                }
                            }) {
                                Icon(Icons.Default.Clear, contentDescription = "Deshacer", tint = Color.Gray)
                            }

                            IconButton(onClick = {
                                drawingPaths = emptyList()
                                currentPath = emptyList()
                            }) {
                                Icon(Icons.Default.Delete, contentDescription = "Limpiar", tint = Color.Gray)
                            }
                        }
                    }
                }
            }
        }
    }

    DisposableEffect(Unit) {
        onDispose {
            watchManager.stopScanning()
        }
    }
}

@Composable
fun ColorButton(color: Color, isSelected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(40.dp)
            .clip(CircleShape)
            .background(color)
            .clickable(onClick = onClick)
            .then(
                if (isSelected) Modifier.padding(2.dp) else Modifier
            ),
        contentAlignment = Alignment.Center
    ) {
        if (isSelected) {
            Icon(
                Icons.Default.Check,
                contentDescription = null,
                tint = if (color == Color.White) Color.Black else Color.White,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

@Composable
fun StrokeButton(width: Float, isSelected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(40.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (isSelected) Color(0xFFE0E0E0) else Color.White)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(width.dp)
                .clip(CircleShape)
                .background(Color.Black)
        )
    }
}

@Composable
fun DrawingCanvas(
    paths: List<DrawingPath>,
    currentPath: List<DrawingPoint>,
    currentColor: Color,
    currentStrokeWidth: Float,
    onPathUpdate: (DrawingPoint) -> Unit,
    onPathEnd: () -> Unit,
    modifier: Modifier = Modifier
) {
    Canvas(
        modifier = modifier
            .background(Color.Transparent)
            .pointerInput(Unit) {
                detectDragGestures(
                    onDragStart = { offset ->
                        onPathUpdate(DrawingPoint(offset.x, offset.y))
                    },
                    onDrag = { change, _ ->
                        change.consume()
                        onPathUpdate(DrawingPoint(change.position.x, change.position.y))
                    },
                    onDragEnd = {
                        onPathEnd()
                    }
                )
            }
    ) {
        // Draw existing paths
        paths.forEach { drawingPath ->
            val path = Path()
            drawingPath.points.forEachIndexed { index, point ->
                if (index == 0) {
                    path.moveTo(point.x, point.y)
                } else {
                    path.lineTo(point.x, point.y)
                }
            }
            drawPath(
                path = path,
                color = Color(drawingPath.color.toULong()),
                style = Stroke(
                    width = drawingPath.strokeWidth,
                    cap = StrokeCap.Round,
                    join = StrokeJoin.Round
                )
            )
        }

        // Draw current path with current color and stroke
        if (currentPath.isNotEmpty()) {
            val path = Path()
            currentPath.forEachIndexed { index, point ->
                if (index == 0) {
                    path.moveTo(point.x, point.y)
                } else {
                    path.lineTo(point.x, point.y)
                }
            }
            drawPath(
                path = path,
                color = currentColor,
                style = Stroke(
                    width = currentStrokeWidth,
                    cap = StrokeCap.Round,
                    join = StrokeJoin.Round
                )
            )
        }
    }
}

// Detectar cartas en el texto completo
fun detectCardsInText(text: String): List<String> {
    val lowerText = text.lowercase()
    val detectedCards = mutableListOf<String>()

    val ranks = listOf("as", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "jota", "sota", "caballo", "reina", "rey")
    val suits = listOf("corazones", "corazón", "diamantes", "diamante", "tréboles", "trébol", "picas", "pica", "copas", "copa", "oros", "oro", "espadas", "espada", "bastos", "basto")

    // Buscar combinaciones de rango + palo
    for (rank in ranks) {
        for (suit in suits) {
            val cardPattern1 = "$rank de $suit"
            val cardPattern2 = "$rank $suit"

            if (lowerText.contains(cardPattern1) || lowerText.contains(cardPattern2)) {
                // Encontramos una carta completa
                val cardName = "$rank de $suit"
                if (!detectedCards.contains(cardName)) {
                    detectedCards.add(cardName)
                }
            }
        }
    }

    // También detectar solo rangos o solo palos como fallback
    if (detectedCards.isEmpty()) {
        for (rank in ranks) {
            if (lowerText.contains(rank)) {
                detectedCards.add(rank)
                break // Solo la primera
            }
        }
    }

    return detectedCards
}

fun isPlayingCard(text: String): Boolean {
    val lowerText = text.lowercase()
    val ranks = listOf("as", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "jota", "sota", "caballo", "reina", "rey", "j", "q", "k")
    val suits = listOf("corazones", "corazón", "diamantes", "diamante", "tréboles", "trébol", "picas", "pica", "copas", "copa", "oros", "oro", "espadas", "espada", "bastos", "basto")

    // Debe contener al menos un rango O un palo para considerarse carta
    val hasRank = ranks.any { lowerText.contains(it) }
    val hasSuit = suits.any { lowerText.contains(it) }

    return hasRank || hasSuit
}

fun createCardDrawing(cardName: String): DrawingPath {
    // Dibujar SOLO el símbolo (número + palo), SIN recuadro
    val points = mutableListOf<DrawingPoint>()
    val random = java.util.Random(cardName.hashCode().toLong())

    // Centro de la pantalla aproximado - MÁS GRANDE
    val centerX = 400f
    val centerY = 500f

    // Parse card name to draw rank and suit
    val lowerName = cardName.lowercase()

    // Draw rank symbol - ARRIBA
    val rankX = centerX - 40f
    val rankY = centerY - 80f

    // Detectar y dibujar el rango de la carta - MÁS GRANDE (escala 5x)
    when {
        lowerName.contains("as") -> addLetterABig(points, rankX, rankY, random)
        lowerName.contains("dos") || lowerName.contains("2") -> addNumber2Big(points, rankX, rankY, random)
        lowerName.contains("tres") || lowerName.contains("3") -> addNumber3Big(points, rankX, rankY, random)
        lowerName.contains("cuatro") || lowerName.contains("4") -> addNumber4Big(points, rankX, rankY, random)
        lowerName.contains("cinco") || lowerName.contains("5") -> addNumber5Big(points, rankX, rankY, random)
        lowerName.contains("seis") || lowerName.contains("6") -> addNumber6Big(points, rankX, rankY, random)
        lowerName.contains("siete") || lowerName.contains("7") -> addNumber7Big(points, rankX, rankY, random)
        lowerName.contains("ocho") || lowerName.contains("8") -> addNumber8Big(points, rankX, rankY, random)
        lowerName.contains("nueve") || lowerName.contains("9") -> addNumber9Big(points, rankX, rankY, random)
        lowerName.contains("diez") || lowerName.contains("10") -> addNumber10Big(points, rankX, rankY, random)
        lowerName.contains("jota") || lowerName.contains("j") -> addLetterJBig(points, rankX, rankY, random)
        lowerName.contains("reina") || lowerName.contains("q") -> addLetterQBig(points, rankX, rankY, random)
        lowerName.contains("rey") || lowerName.contains("k") -> addLetterKBig(points, rankX, rankY, random)
        lowerName.contains("sota") -> addLetterSBig(points, rankX, rankY, random)
        lowerName.contains("caballo") -> addLetterCBig(points, rankX, rankY, random)
    }

    // Draw suit symbol ABAJO - MÁS GRANDE
    val suitY = centerY + 40f
    when {
        lowerName.contains("corazones") || lowerName.contains("corazón") -> addHeartBig(points, centerX, suitY, random)
        lowerName.contains("picas") || lowerName.contains("pica") -> addSpadeBig(points, centerX, suitY, random)
        lowerName.contains("diamantes") || lowerName.contains("diamante") -> addDiamondBig(points, centerX, suitY, random)
        lowerName.contains("tréboles") || lowerName.contains("trébol") -> addClubBig(points, centerX, suitY, random)
        lowerName.contains("copas") || lowerName.contains("copa") -> addCupBig(points, centerX, suitY, random)
        lowerName.contains("oros") || lowerName.contains("oro") -> addCoinBig(points, centerX, suitY, random)
        lowerName.contains("espadas") || lowerName.contains("espada") -> addSwordBig(points, centerX, suitY, random)
        lowerName.contains("bastos") || lowerName.contains("basto") -> addClubBig(points, centerX, suitY, random)
    }

    return DrawingPath(
        points = points,
        color = Color.Black.value.toLong(),
        strokeWidth = 8f  // Más grueso
    )
}

// Versiones GRANDES (5x) - para números
fun addNumber2Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y + 3*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 3*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
}

fun addNumber3Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 3*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 9*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
}

fun addNumber4Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 8*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 8*scale)))
    points.add(DrawingPoint(addNoise(x + 6*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 6*scale), addNoise(y + 12*scale)))
}

fun addNumber5Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
}

fun addNumber6Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6*scale)))
}

fun addNumber7Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y + 12*scale)))
}

fun addNumber8Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y + 3*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 3*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 3*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 9*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 9*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y + 6*scale)))
}

fun addNumber9Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
}

fun addNumber10Big(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 1*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 1*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 9*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 9*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y)))
}

// Versiones GRANDES - letras
fun addLetterABig(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 4*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 2*scale), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x + 6*scale), addNoise(y + 6*scale)))
}

fun addLetterJBig(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 6*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 6*scale), addNoise(y + 10*scale)))
    points.add(DrawingPoint(addNoise(x + 2*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 10*scale)))
}

fun addLetterQBig(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 10*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 10*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 5*scale), addNoise(y + 7*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
}

fun addLetterKBig(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
}

fun addLetterSBig(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 2*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 5*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 7*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 10*scale)))
}

fun addLetterCBig(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val scale = 5f
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 3f }
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12*scale)))
    points.add(DrawingPoint(addNoise(x + 8*scale), addNoise(y + 12*scale)))
}

// Versiones GRANDES - palos
fun addHeartBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 60f
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 1.5f)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy - size)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
}

fun addSpadeBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 60f
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
}

fun addDiamondBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 70f
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 2)))
}

fun addClubBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 50f
    for (angle in 0..360 step 30) {
        val rad = Math.toRadians(angle.toDouble())
        points.add(DrawingPoint(
            addNoise(cx + (size / 3 * Math.cos(rad)).toFloat()),
            addNoise(cy - size / 2 + (size / 3 * Math.sin(rad)).toFloat())
        ))
    }
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
}

fun addCupBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 60f
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 4), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx + size / 4), addNoise(cy + size / 2)))
}

fun addCoinBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 60f
    for (angle in 0..360 step 20) {
        val rad = Math.toRadians(angle.toDouble())
        points.add(DrawingPoint(
            addNoise(cx + (size / 2 * Math.cos(rad)).toFloat()),
            addNoise(cy + (size / 2 * Math.sin(rad)).toFloat())
        ))
    }
}

fun addSwordBig(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 4f }
    val size = 70f
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 4)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy + size / 4)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy + size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
}

// Funciones para dibujar números (originales - mantener para compatibilidad)
fun addNumber2(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y + 3)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 3)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
}

fun addNumber3(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 3)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 9)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
}

fun addNumber4(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 8)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 8)))
    points.add(DrawingPoint(addNoise(x + 6), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 6), addNoise(y + 12)))
}

fun addNumber5(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
}

fun addNumber6(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6)))
}

fun addNumber7(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y + 12)))
}

fun addNumber8(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y + 3)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 3)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 3)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 9)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 9)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y + 6)))
}

fun addNumber9(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
}

fun addNumber10(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    // "1"
    points.add(DrawingPoint(addNoise(x + 1), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 1), addNoise(y + 12)))
    // "0"
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 9), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 9), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y)))
}

// Funciones para dibujar letras
fun addLetterA(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 4), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 2), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x + 6), addNoise(y + 6)))
}

fun addLetterJ(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x + 6), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 6), addNoise(y + 10)))
    points.add(DrawingPoint(addNoise(x + 2), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 10)))
}

fun addLetterQ(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 10)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 10)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x + 5), addNoise(y + 7)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
}

fun addLetterK(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 6)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
}

fun addLetterS(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 2)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 5)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 7)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 10)))
}

fun addLetterC(points: MutableList<DrawingPoint>, x: Float, y: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 1.5f }
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y)))
    points.add(DrawingPoint(addNoise(x), addNoise(y + 12)))
    points.add(DrawingPoint(addNoise(x + 8), addNoise(y + 12)))
}

fun addHeart(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 25f
    // Heart shape
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 1.5f)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy - size)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
}

fun addSpade(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 25f
    // Spade shape (inverted heart with stem)
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy - size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
}

fun addDiamond(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 30f
    // Diamond shape
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 2)))
}

fun addClub(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 20f
    // Three circles forming a club
    // Top circle
    for (angle in 0..360 step 30) {
        val rad = Math.toRadians(angle.toDouble())
        points.add(DrawingPoint(
            addNoise(cx + (size / 3 * Math.cos(rad)).toFloat()),
            addNoise(cy - size / 2 + (size / 3 * Math.sin(rad)).toFloat())
        ))
    }
    // Stem
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
}

// Palos españoles
fun addCup(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 25f
    // Copa shape - like a chalice
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx + size / 2), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy - size / 2)))
    // Stem
    points.add(DrawingPoint(addNoise(cx), addNoise(cy)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx - size / 4), addNoise(cy + size / 2)))
    points.add(DrawingPoint(addNoise(cx + size / 4), addNoise(cy + size / 2)))
}

fun addCoin(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 25f
    // Oro shape - like a coin/circle
    for (angle in 0..360 step 20) {
        val rad = Math.toRadians(angle.toDouble())
        points.add(DrawingPoint(
            addNoise(cx + (size / 2 * Math.cos(rad)).toFloat()),
            addNoise(cy + (size / 2 * Math.sin(rad)).toFloat())
        ))
    }
}

fun addSword(points: MutableList<DrawingPoint>, cx: Float, cy: Float, random: java.util.Random) {
    val addNoise = { value: Float -> value + (random.nextFloat() - 0.5f) * 2f }
    val size = 30f
    // Espada shape - like a sword
    // Blade
    points.add(DrawingPoint(addNoise(cx), addNoise(cy - size / 2)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 4)))
    // Cross guard
    points.add(DrawingPoint(addNoise(cx - size / 3), addNoise(cy + size / 4)))
    points.add(DrawingPoint(addNoise(cx + size / 3), addNoise(cy + size / 4)))
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 4)))
    // Handle
    points.add(DrawingPoint(addNoise(cx), addNoise(cy + size / 2)))
}
