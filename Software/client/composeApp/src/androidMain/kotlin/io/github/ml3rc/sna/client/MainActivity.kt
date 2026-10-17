package io.github.ml3rc.sna.client

import android.app.Application
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.ui.Modifier
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModelProvider
import io.github.ml3rc.sna.client.data.AndroidFileContext
import io.github.ml3rc.sna.client.transport.AndroidSerialTransport
import io.github.ml3rc.sna.client.ui.App
import io.github.ml3rc.sna.client.session.AppState

/** Owns the USB session across activity recreation, without retaining an old Activity. */
class SnaSessionViewModel(application: Application) : AndroidViewModel(application) {
    val app = AppState(AndroidSerialTransport(application))

    override fun onCleared() {
        app.close()
        super.onCleared()
    }
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AndroidFileContext.appContext = applicationContext
        val owner = ViewModelProvider(this)[SnaSessionViewModel::class.java]
        setContent {
            App(app = owner.app, compactUi = true,
                modifier = Modifier.safeDrawingPadding().imePadding())
        }
    }
}
