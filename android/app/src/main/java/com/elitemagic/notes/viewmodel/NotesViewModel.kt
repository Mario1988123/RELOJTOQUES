package com.elitemagic.notes.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.elitemagic.notes.data.NotesRepository
import com.elitemagic.notes.model.Note
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class NotesViewModel(private val repository: NotesRepository) : ViewModel() {

    val notes: StateFlow<List<Note>> = repository.notes

    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery.asStateFlow()

    private val _filteredNotes = MutableStateFlow<List<Note>>(emptyList())
    val filteredNotes: StateFlow<List<Note>> = _filteredNotes.asStateFlow()

    init {
        viewModelScope.launch {
            notes.collect { allNotes ->
                filterNotes(allNotes, _searchQuery.value)
            }
        }

        viewModelScope.launch {
            searchQuery.collect { query ->
                filterNotes(notes.value, query)
            }
        }
    }

    private fun filterNotes(allNotes: List<Note>, query: String) {
        _filteredNotes.value = if (query.isEmpty()) {
            allNotes
        } else {
            allNotes.filter { note ->
                note.title.contains(query, ignoreCase = true) ||
                note.content.contains(query, ignoreCase = true)
            }
        }
    }

    fun updateSearchQuery(query: String) {
        _searchQuery.value = query
    }

    fun addNote(note: Note) {
        repository.addNote(note)
    }

    fun updateNote(note: Note) {
        repository.updateNote(note)
    }

    fun deleteNote(noteId: String) {
        repository.deleteNote(noteId)
    }

    fun getNoteById(noteId: String): Note? {
        return repository.getNoteById(noteId)
    }
}

class NotesViewModelFactory(private val repository: NotesRepository) : ViewModelProvider.Factory {
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(NotesViewModel::class.java)) {
            @Suppress("UNCHECKED_CAST")
            return NotesViewModel(repository) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class")
    }
}
