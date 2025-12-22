package com.elitemagic.notes.data

import android.content.Context
import com.elitemagic.notes.model.Note
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class NotesRepository(private val context: Context) {
    private val prefs = context.getSharedPreferences("notes_prefs", Context.MODE_PRIVATE)
    private val gson = Gson()

    private val _notes = MutableStateFlow<List<Note>>(emptyList())
    val notes: StateFlow<List<Note>> = _notes.asStateFlow()

    init {
        loadNotes()
    }

    private fun loadNotes() {
        val notesJson = prefs.getString("notes", "[]")
        val type = object : TypeToken<List<Note>>() {}.type
        val loadedNotes: List<Note> = gson.fromJson(notesJson, type) ?: emptyList()
        _notes.value = loadedNotes.sortedByDescending { it.updatedAt }
    }

    private fun saveNotes() {
        val notesJson = gson.toJson(_notes.value)
        prefs.edit().putString("notes", notesJson).apply()
    }

    fun addNote(note: Note) {
        _notes.value = listOf(note) + _notes.value
        saveNotes()
    }

    fun updateNote(note: Note) {
        val updatedNote = note.copy(updatedAt = System.currentTimeMillis())
        _notes.value = _notes.value.map {
            if (it.id == updatedNote.id) updatedNote else it
        }.sortedByDescending { it.updatedAt }
        saveNotes()
    }

    fun deleteNote(noteId: String) {
        _notes.value = _notes.value.filter { it.id != noteId }
        saveNotes()
    }

    fun getNoteById(noteId: String): Note? {
        return _notes.value.find { it.id == noteId }
    }
}
