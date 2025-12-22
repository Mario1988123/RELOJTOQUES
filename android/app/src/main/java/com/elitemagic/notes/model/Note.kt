package com.elitemagic.notes.model

import java.util.Date
import java.util.UUID

data class Note(
    val id: String = UUID.randomUUID().toString(),
    val title: String = "",
    val content: String = "",
    val drawingData: String? = null, // JSON string of drawing paths
    val createdAt: Long = System.currentTimeMillis(),
    val updatedAt: Long = System.currentTimeMillis(),
    val isImageNote: Boolean = false
)

data class DrawingPath(
    val points: List<DrawingPoint>,
    val color: Long,
    val strokeWidth: Float
)

data class DrawingPoint(
    val x: Float,
    val y: Float
)
