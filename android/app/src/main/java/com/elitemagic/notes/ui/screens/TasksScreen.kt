package com.elitemagic.notes.ui.screens

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

data class DrawingPath(
    val points: MutableList<Offset> = mutableListOf(),
    val color: Color = Color.Black,
    val strokeWidth: Float = 5f
)

data class CardDrawing(
    val name: String,
    val suit: String,
    val paths: MutableList<DrawingPath> = mutableListOf()
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TasksScreen(
    onNavigateToNotes: () -> Unit
) {
    var showCardMenu by remember { mutableStateOf(false) }
    var longPressTriggered by remember { mutableStateOf(false) }
    val coroutineScope = rememberCoroutineScope()

    // Crear las 52 cartas de poker
    val cards = remember {
        val suits = listOf("♥ Corazones", "♦ Diamantes", "♣ Tréboles", "♠ Picas")
        val ranks = listOf("As", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K")
        val cardsList = mutableListOf<CardDrawing>()

        for (suit in suits) {
            for (rank in ranks) {
                cardsList.add(CardDrawing(rank, suit))
            }
        }
        cardsList
    }

    if (showCardMenu) {
        CardMenuScreen(
            cards = cards,
            onClose = { showCardMenu = false }
        )
    } else {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Text(
                            "Tareas",
                            fontSize = 24.sp,
                            fontWeight = FontWeight.Normal
                        )
                    },
                    actions = {
                        IconButton(onClick = { /* TODO: Open folder view */ }) {
                            Icon(Icons.Default.Folder, contentDescription = "Carpetas")
                        }
                        IconButton(onClick = { /* TODO: Open settings */ }) {
                            Icon(Icons.Default.Settings, contentDescription = "Configuración")
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = Color.White,
                        titleContentColor = Color.Black
                    )
                )
            },
            bottomBar = {
                NavigationBar(
                    containerColor = Color.White
                ) {
                    NavigationBarItem(
                        selected = false,
                        onClick = onNavigateToNotes,
                        icon = {
                            Icon(
                                Icons.Default.Description,
                                contentDescription = "Notas"
                            )
                        },
                        label = { Text("Notas") }
                    )
                    NavigationBarItem(
                        selected = true,
                        onClick = { },
                        icon = {
                            Icon(
                                Icons.Default.CheckCircle,
                                contentDescription = "Tareas"
                            )
                        },
                        label = { Text("Tareas") }
                    )
                }
            }
        ) { paddingValues ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(paddingValues)
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onLongPress = {
                                if (!longPressTriggered) {
                                    longPressTriggered = true
                                    showCardMenu = true
                                }
                            }
                        )
                    },
                contentAlignment = Alignment.Center
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    Icon(
                        Icons.Default.Info,
                        contentDescription = "Info",
                        modifier = Modifier.size(64.dp),
                        tint = Color.Gray
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        "Mantén pulsado para acceder\nal menú de cartas mágicas",
                        style = MaterialTheme.typography.bodyLarge,
                        color = Color.Gray,
                        textAlign = TextAlign.Center
                    )
                }
            }
        }
    }

    // Reset del trigger cuando se cierra el menú
    LaunchedEffect(showCardMenu) {
        if (!showCardMenu) {
            delay(500)
            longPressTriggered = false
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardMenuScreen(
    cards: List<CardDrawing>,
    onClose: () -> Unit
) {
    var selectedCard by remember { mutableStateOf<CardDrawing?>(null) }

    if (selectedCard != null) {
        CardDrawingScreen(
            card = selectedCard!!,
            onBack = { selectedCard = null }
        )
    } else {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Text(
                            "Baraja de Poker - 52 Cartas",
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold
                        )
                    },
                    navigationIcon = {
                        IconButton(onClick = onClose) {
                            Icon(Icons.Default.Close, contentDescription = "Cerrar")
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.primaryContainer,
                        titleContentColor = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                )
            }
        ) { paddingValues ->
            LazyVerticalGrid(
                columns = GridCells.Fixed(2),
                contentPadding = paddingValues,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(cards) { card ->
                    CardItem(
                        card = card,
                        onClick = { selectedCard = card }
                    )
                }
            }
        }
    }
}

@Composable
fun CardItem(
    card: CardDrawing,
    onClick: () -> Unit
) {
    val suitColor = when {
        card.suit.contains("♥") || card.suit.contains("♦") -> Color(0xFFE53935)
        else -> Color.Black
    }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .height(100.dp),
        onClick = onClick,
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(
            containerColor = Color.White
        ),
        elevation = CardDefaults.cardElevation(
            defaultElevation = 4.dp
        )
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(12.dp),
            contentAlignment = Alignment.Center
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                Text(
                    text = card.name,
                    fontSize = 32.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.Black
                )
                Text(
                    text = card.suit,
                    fontSize = 14.sp,
                    color = suitColor,
                    fontWeight = FontWeight.Medium
                )

                // Indicador si ya tiene dibujos
                if (card.paths.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Icon(
                        Icons.Default.Check,
                        contentDescription = "Dibujada",
                        modifier = Modifier.size(16.dp),
                        tint = Color(0xFF4CAF50)
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardDrawingScreen(
    card: CardDrawing,
    onBack: () -> Unit
) {
    var currentPath by remember { mutableStateOf<DrawingPath?>(null) }
    var strokeWidth by remember { mutableStateOf(5f) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(
                            "${card.name} de ${card.suit}",
                            fontSize = 18.sp,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            "Dibuja la carta con tu dedo",
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Normal
                        )
                    }
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Volver")
                    }
                },
                actions = {
                    // Botón para borrar todo
                    IconButton(onClick = {
                        card.paths.clear()
                    }) {
                        Icon(Icons.Default.Delete, contentDescription = "Borrar todo")
                    }
                    // Botón para deshacer último trazo
                    IconButton(
                        onClick = {
                            if (card.paths.isNotEmpty()) {
                                card.paths.removeAt(card.paths.size - 1)
                            }
                        },
                        enabled = card.paths.isNotEmpty()
                    ) {
                        Icon(Icons.Default.Undo, contentDescription = "Deshacer")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer,
                    titleContentColor = MaterialTheme.colorScheme.onPrimaryContainer
                )
            )
        },
        bottomBar = {
            // Controles de grosor
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = Color.White,
                shadowElevation = 8.dp
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceEvenly,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Grosor:", fontWeight = FontWeight.Medium)

                    Button(
                        onClick = { strokeWidth = 3f },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (strokeWidth == 3f) MaterialTheme.colorScheme.primary else Color.LightGray
                        )
                    ) {
                        Text("Fino")
                    }

                    Button(
                        onClick = { strokeWidth = 5f },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (strokeWidth == 5f) MaterialTheme.colorScheme.primary else Color.LightGray
                        )
                    ) {
                        Text("Normal")
                    }

                    Button(
                        onClick = { strokeWidth = 8f },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (strokeWidth == 8f) MaterialTheme.colorScheme.primary else Color.LightGray
                        )
                    ) {
                        Text("Grueso")
                    }
                }
            }
        }
    ) { paddingValues ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .background(Color.White)
        ) {
            Canvas(
                modifier = Modifier
                    .fillMaxSize()
                    .border(2.dp, Color.LightGray)
                    .pointerInput(Unit) {
                        detectDragGestures(
                            onDragStart = { offset ->
                                currentPath = DrawingPath(
                                    points = mutableListOf(offset),
                                    color = Color.Black,
                                    strokeWidth = strokeWidth
                                )
                            },
                            onDrag = { change, _ ->
                                currentPath?.points?.add(change.position)
                            },
                            onDragEnd = {
                                currentPath?.let {
                                    if (it.points.size > 1) {
                                        card.paths.add(it)
                                    }
                                }
                                currentPath = null
                            }
                        )
                    }
            ) {
                // Dibujar todos los trazos guardados
                card.paths.forEach { path ->
                    drawPath(path)
                }

                // Dibujar el trazo actual
                currentPath?.let { path ->
                    drawPath(path)
                }
            }
        }
    }
}

// Extensión para dibujar un path
private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawPath(drawingPath: DrawingPath) {
    if (drawingPath.points.size < 2) return

    val path = Path()
    path.moveTo(drawingPath.points[0].x, drawingPath.points[0].y)

    for (i in 1 until drawingPath.points.size) {
        val prevPoint = drawingPath.points[i - 1]
        val currentPoint = drawingPath.points[i]
        val midPoint = Offset(
            (prevPoint.x + currentPoint.x) / 2,
            (prevPoint.y + currentPoint.y) / 2
        )
        path.quadraticBezierTo(
            prevPoint.x, prevPoint.y,
            midPoint.x, midPoint.y
        )
    }

    val lastPoint = drawingPath.points.last()
    path.lineTo(lastPoint.x, lastPoint.y)

    drawPath(
        path = path,
        color = drawingPath.color,
        style = Stroke(
            width = drawingPath.strokeWidth,
            cap = StrokeCap.Round,
            join = StrokeJoin.Round
        )
    )
}
