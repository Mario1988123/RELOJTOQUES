package com.elitemagic.notes

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.elitemagic.notes.data.NotesRepository
import com.elitemagic.notes.model.Note
import com.elitemagic.notes.ui.screens.NoteEditorScreen
import com.elitemagic.notes.ui.screens.NotesListScreen
import com.elitemagic.notes.ui.screens.TasksScreen
import com.elitemagic.notes.ui.theme.EliteMagicNotesTheme
import com.elitemagic.notes.viewmodel.NotesViewModel
import com.elitemagic.notes.viewmodel.NotesViewModelFactory
import com.elitemagic.notes.watch.WatchBeaconManager

class MainActivity : ComponentActivity() {

    private lateinit var watchBeaconManager: WatchBeaconManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Install splash screen
        installSplashScreen()

        watchBeaconManager = WatchBeaconManager(this)

        setContent {
            EliteMagicNotesTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    NotesApp(watchBeaconManager)
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        watchBeaconManager.destroy()
    }
}

@Composable
fun NotesApp(watchManager: WatchBeaconManager) {
    val navController = rememberNavController()
    val repository = NotesRepository(androidx.compose.ui.platform.LocalContext.current)
    val viewModel: NotesViewModel = viewModel(
        factory = NotesViewModelFactory(repository)
    )

    NavHost(
        navController = navController,
        startDestination = "notes_list"
    ) {
        composable("notes_list") {
            val notes by viewModel.filteredNotes.collectAsState()

            NotesListScreen(
                notes = notes,
                onNoteClick = { note ->
                    navController.navigate("note_editor/${note.id}")
                },
                onNewNoteClick = {
                    navController.navigate("note_editor/new")
                },
                onNavigateToTasks = {
                    navController.navigate("tasks")
                }
            )
        }

        composable("tasks") {
            TasksScreen(
                onNavigateToNotes = {
                    navController.navigate("notes_list") {
                        popUpTo("notes_list") { inclusive = true }
                    }
                }
            )
        }

        composable(
            route = "note_editor/{noteId}",
            arguments = listOf(navArgument("noteId") { type = NavType.StringType })
        ) { backStackEntry ->
            val noteId = backStackEntry.arguments?.getString("noteId")
            val note = if (noteId != "new") {
                viewModel.getNoteById(noteId ?: "")
            } else {
                null
            }

            NoteEditorScreen(
                note = note,
                watchManager = watchManager,
                onSave = { savedNote ->
                    if (note == null) {
                        viewModel.addNote(savedNote)
                    } else {
                        viewModel.updateNote(savedNote)
                    }
                },
                onBack = {
                    navController.popBackStack()
                }
            )
        }
    }
}
