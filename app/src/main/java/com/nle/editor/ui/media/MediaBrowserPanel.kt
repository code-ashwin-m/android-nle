package com.nle.editor.ui.media

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.nle.editor.engine.MediaType
import com.nle.editor.viewmodel.EditorViewModel

private data class ImportedMediaItem(val sourceId: Long, val uri: String, val type: MediaType)

/**
 * "Media library should reference imported assets rather than duplicating
 * files" -- the import flow below only ever passes a content URI across
 * JNI (see EditorEngine::ImportMedia / MediaSource.h); no file is copied
 * or read into memory here. The list shown is kept client-side for Phase 1
 * (see EditorViewModel.onImportMedia's comment) rather than queried from
 * native, since there's no native "list all media" call yet -- that's the
 * natural next addition once proxy/thumbnail generation needs its own
 * persisted media table anyway.
 */
@Composable
fun MediaBrowserPanel(viewModel: EditorViewModel, modifier: Modifier = Modifier) {
    var importedItems by remember { mutableStateOf(listOf<ImportedMediaItem>()) }

    val pickVideoLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let {
            val sourceId = viewModel.onImportMedia(it.toString(), MediaType.VIDEO)
            importedItems = importedItems + ImportedMediaItem(sourceId, it.toString(), MediaType.VIDEO)
        }
    }

    Column(modifier = modifier.padding(12.dp)) {
        Button(onClick = { pickVideoLauncher.launch("video/*") }) { Text("Import Video") }
        LazyColumn {
            items(importedItems) { item ->
                Text(text = item.uri, modifier = Modifier.padding(vertical = 6.dp))
            }
        }
    }
}
