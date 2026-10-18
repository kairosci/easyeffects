/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import android.widget.PopupMenu
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.ViewCompat
import androidx.core.view.GravityCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.drawerlayout.widget.DrawerLayout
import org.json.JSONObject
import java.io.IOException

class MainActivity : ComponentActivity() {
    private val model: EqualizerModel get() = AudioRuntime.model
    private val nativeEqualizer: NativeEqualizer get() = AudioRuntime.nativeEqualizer
    private lateinit var graph: EqualizerGraphView
    private lateinit var drawerLayout: DrawerLayout
    private lateinit var workspace: android.widget.FrameLayout
    private lateinit var pluginPage: View
    private val pluginScreens = linkedMapOf<String, View>()
    private val pluginContents = linkedMapOf<String, LinearLayout>()
    private var activePlugin = "equalizer"
    private lateinit var appPage: View
    private lateinit var aboutPage: View
    private lateinit var bottomNavigation: LinearLayout
    private val bottomNavigationItems = mutableMapOf<MainSection, LinearLayout>()
    private var currentSection = MainSection.PLUGINS
    private var processingTarget = ProcessingTarget.OUTPUT
    private lateinit var mainScroll: ScrollView
    private lateinit var mainContentColumn: LinearLayout
    private lateinit var drawerIcon: android.widget.ImageButton
    private lateinit var pluginHeader: View
    private lateinit var outputRouteAction: android.widget.ImageButton
    private lateinit var inputRouteAction: android.widget.ImageButton
    private lateinit var bandStatus: TextView
    private lateinit var globalSwitch: Switch
    private lateinit var globalStatus: TextView
    private lateinit var preampText: TextView
    private lateinit var bandCountText: TextView
    private lateinit var selectedBandSpinner: Spinner
    private lateinit var qualityText: TextView
    private lateinit var qualitySeek: SeekBar
    private lateinit var enabledSwitch: Switch
    private lateinit var soloSwitch: Switch
    private lateinit var bypassSwitch: Switch
    private lateinit var typeSpinner: Spinner
    private lateinit var channelSpinner: Spinner
    private lateinit var editBandAction: TextView
    private val platformEffectSwitches = mutableMapOf<AndroidPlatformEffect, Switch>()
    private val dynamicsStageSwitches = mutableMapOf<AndroidDynamicsStage, Switch>()
    private var updatingControls = false
    private var refreshingGlobalSwitch = false
    private val statusHandler = Handler(Looper.getMainLooper())
    private val statusUpdater = object : Runnable {
        override fun run() { refreshGlobalStatus(); statusHandler.postDelayed(this, 700) }
    }

    private val importPreset = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) importPreset(uri) }
    private val exportJsonPreset = registerForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri -> if (uri != null) writePreset(uri, model.toJson().toString(2)) }
    private val exportApoPreset = registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri -> if (uri != null) writePreset(uri, model.toEqualizerApo()) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        processingTarget = if (getSharedPreferences("audio_target", MODE_PRIVATE).getBoolean("input", false)) {
            ProcessingTarget.INPUT
        } else {
            ProcessingTarget.OUTPUT
        }
        WindowCompat.setDecorFitsSystemWindows(window, false)
        window.statusBarColor = getColor(R.color.ee_background)
        window.navigationBarColor = getColor(R.color.ee_background)
        WindowCompat.getInsetsController(window, window.decorView).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }
        val root = createContent()
        ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            view.setPadding(dp(12) + bars.left, dp(8) + bars.top, dp(12) + bars.right, dp(8) + bars.bottom)
            insets
        }
        setContentView(root)
        model.addChangeListener { AudioRuntime.globalOutputEqualizer.update(); refreshLabels() }
        refreshLabels()
        if (processingTarget == ProcessingTarget.OUTPUT && AudioRuntime.hasEnabledOutputPlugins()) {
            runCatching { GlobalEqualizerService.start(this) }.onFailure { failure ->
                AudioRuntime.globalOutputEqualizer.recordFailure(failure.localizedMessage ?: failure.javaClass.simpleName)
                showMessage(getString(R.string.global_eq_enable_failed, failure.localizedMessage ?: "unknown error"))
            }
        }
        runRequestedCppBackendChecks(intent)
    }

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        runRequestedCppBackendChecks(intent)
    }

    private fun runRequestedCppBackendChecks(intent: android.content.Intent) {
        val isDebugBuild = applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE != 0
        if (isDebugBuild && intent.getBooleanExtra("runCppBackendChecks", false)) {
            if (AudioRuntime.platformEffects.hasEnabledEffects() || AudioRuntime.dynamicsProcessing.hasEnabledStages()) {
                android.util.Log.w("EasyEffectsNativeTests", "SKIP: disable Android system effects before running backend checks")
                return
            }
            android.util.Log.i("EasyEffectsNativeTests", nativeEqualizer.runAndroidBackendChecks())
        }
        if (isDebugBuild && intent.getBooleanExtra("runPcmPlaybackTest", false)) {
            val audioManager = getSystemService(android.media.AudioManager::class.java)
            if (audioManager.isMusicActive) {
                android.util.Log.w("EasyEffectsPcmPreview", "SKIP: pause other music before the short PCM playback test")
            } else if (audioManager.isStreamMute(AudioManager.STREAM_MUSIC) ||
                audioManager.getStreamVolume(AudioManager.STREAM_MUSIC) == 0
            ) {
                android.util.Log.e("EasyEffectsPcmPreview", "FAIL: media output is muted; unmute the device to verify audible PCM playback")
            } else {
                Thread {
                    android.util.Log.i("EasyEffectsPcmPreview", nativeEqualizer.runPcmPlaybackTest())
                }.start()
            }
        }
    }

    override fun onStart() { super.onStart(); refreshGlobalStatus(); statusHandler.post(statusUpdater) }
    override fun onStop() { statusHandler.removeCallbacks(statusUpdater); super.onStop() }

    override fun onResume() {
        super.onResume()
        if (::outputRouteAction.isInitialized) refreshRouteLabels()
    }

    private fun createContent(): View {
        val appLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(getColor(R.color.ee_background))
        }
        drawerLayout = DrawerLayout(this).apply {
            setBackgroundColor(getColor(R.color.ee_background))
            isSaveEnabled = true
        }
        workspace = android.widget.FrameLayout(this)
        pluginPage = createGraphPane()
        appPage = createAppPage()
        aboutPage = createAboutPage()
        workspace.addView(pluginPage, android.widget.FrameLayout.LayoutParams(-1, -1))
        workspace.addView(appPage, android.widget.FrameLayout.LayoutParams(-1, -1))
        workspace.addView(aboutPage, android.widget.FrameLayout.LayoutParams(-1, -1))
        drawerLayout.addView(workspace, DrawerLayout.LayoutParams(-1, -1))
        drawerLayout.addView(createSidebar(), DrawerLayout.LayoutParams(dp(320), -1, GravityCompat.START))
        drawerLayout.addDrawerListener(object : DrawerLayout.SimpleDrawerListener() {
            override fun onDrawerOpened(drawerView: View) = updateDrawerIcon(true)
            override fun onDrawerClosed(drawerView: View) = updateDrawerIcon(false)
        })
        appLayout.addView(createAppBar(), LinearLayout.LayoutParams(-1, -2).apply {
            leftMargin = dp(8)
            topMargin = dp(4)
            rightMargin = dp(8)
            bottomMargin = dp(4)
        })
        appLayout.addView(drawerLayout, LinearLayout.LayoutParams(-1, 0, 1f))
        appLayout.addView(createBottomNavigation(), LinearLayout.LayoutParams(-1, dp(64)))
        showSection(MainSection.PLUGINS)
        return appLayout
    }

    private fun createAppBar(): View {
        val appBar = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(4), dp(2), dp(8), dp(2))
        }
        val header = horizontalRow()
        pluginHeader = header
        drawerIcon = android.widget.ImageButton(this).apply {
            setImageResource(R.drawable.ic_plugins_menu)
            imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.ee_text_secondary))
            contentDescription = getString(R.string.open_plugins)
            setBackgroundResource(R.drawable.ee_action_background)
            isFocusable = true
            setOnClickListener {
                if (drawerLayout.isDrawerOpen(GravityCompat.START)) drawerLayout.closeDrawer(GravityCompat.START)
                else drawerLayout.openDrawer(GravityCompat.START)
            }
        }
        header.addView(drawerIcon, LinearLayout.LayoutParams(dp(48), dp(48)).apply { marginEnd = dp(12) })
        header.addView(View(this), LinearLayout.LayoutParams(0, 1, 1f))
        outputRouteAction = routeAction(R.drawable.ic_audio_output, ProcessingTarget.OUTPUT)
        inputRouteAction = routeAction(R.drawable.ic_audio_input, ProcessingTarget.INPUT)
        header.addView(outputRouteAction, LinearLayout.LayoutParams(dp(48), dp(48)).apply { marginEnd = dp(8) })
        header.addView(inputRouteAction, LinearLayout.LayoutParams(dp(48), dp(48)))
        appBar.addView(header, LinearLayout.LayoutParams(-1, -2))
        appBar.addView(View(this).apply { setBackgroundColor(getColor(R.color.ee_outline)) },
            LinearLayout.LayoutParams(-1, dp(1)))
        refreshRouteLabels()
        return appBar
    }

    private fun routeAction(icon: Int, target: ProcessingTarget) = android.widget.ImageButton(this).apply {
        setImageResource(icon)
        scaleType = android.widget.ImageView.ScaleType.CENTER_INSIDE
        contentDescription = getString(R.string.route_unknown)
        setBackgroundResource(R.drawable.ee_route_background)
        setOnClickListener {
            selectProcessingTarget(target)
            if (target == ProcessingTarget.INPUT) showInputRouteDialog() else showOutputRouteDialog()
        }
    }

    private fun refreshRouteLabels() {
        if (!::outputRouteAction.isInitialized || !::inputRouteAction.isInitialized) return
        val audioManager = getSystemService(AudioManager::class.java)
        val output = getPreferredMediaOutputs(audioManager).firstOrNull()
        val input = audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS).firstOrNull()
        outputRouteAction.contentDescription = getString(
            if (processingTarget == ProcessingTarget.OUTPUT) R.string.route_output_selected else R.string.route_output_choose,
            output?.let(::deviceLabel) ?: getString(R.string.route_unknown),
        )
        inputRouteAction.contentDescription = getString(
            if (processingTarget == ProcessingTarget.INPUT) R.string.route_input_selected else R.string.route_input_choose,
            input?.let(::deviceLabel) ?: getString(R.string.route_unknown),
        )
        refreshTargetSelection()
    }

    private fun selectProcessingTarget(target: ProcessingTarget) {
        if (processingTarget == target) return
        processingTarget = target
        getSharedPreferences("audio_target", MODE_PRIVATE).edit().putBoolean("input", target == ProcessingTarget.INPUT).apply()
        if (target == ProcessingTarget.INPUT) disableOutputProcessing()
        refreshTargetSelection()
        refreshGlobalStatus()
    }

    private fun refreshTargetSelection() {
        if (!::outputRouteAction.isInitialized || !::inputRouteAction.isInitialized) return
        listOf(outputRouteAction to ProcessingTarget.OUTPUT, inputRouteAction to ProcessingTarget.INPUT).forEach { (button, target) ->
            val selected = processingTarget == target
            button.isSelected = selected
            button.imageTintList = android.content.res.ColorStateList.valueOf(
                getColor(if (selected) R.color.ee_accent else R.color.ee_text_secondary),
            )
        }
        refreshProcessingTargetControls()
    }

    private fun refreshProcessingTargetControls() {
        val outputSelected = processingTarget == ProcessingTarget.OUTPUT
        if (::globalSwitch.isInitialized) globalSwitch.isEnabled = outputSelected
        platformEffectSwitches.values.forEach { it.isEnabled = outputSelected }
        dynamicsStageSwitches.values.forEach { it.isEnabled = outputSelected }
    }

    private fun disableOutputProcessing() {
        AudioRuntime.dynamicsProcessing.disableAll()
        AudioRuntime.platformEffects.disableAll()
        AudioRuntime.globalOutputEqualizer.disable()
        updatingControls = true
        dynamicsStageSwitches.values.forEach { it.isChecked = false }
        platformEffectSwitches.values.forEach { it.isChecked = false }
        if (::globalSwitch.isInitialized) globalSwitch.isChecked = false
        updatingControls = false
        GlobalEqualizerService.stop(this)
    }

    private fun getPreferredMediaOutputs(audioManager: AudioManager): List<AudioDeviceInfo> =
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            audioManager.getAudioDevicesForAttributes(
                AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build(),
            )
        } else {
            audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).take(1)
        }

    private fun deviceLabel(device: AudioDeviceInfo): String {
        val kind = getString(when (device.type) {
            AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> R.string.route_builtin_speaker
            AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> R.string.route_earpiece
            AudioDeviceInfo.TYPE_BUILTIN_MIC -> R.string.route_builtin_mic
            AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_WIRED_HEADSET -> R.string.route_wired
            AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> R.string.route_bluetooth
            AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET -> R.string.route_usb
            else -> R.string.route_audio_device
        })
        val product = device.productName.toString().trim()
        return if (product.isEmpty()) kind else "$product · $kind"
    }

    private fun showOutputRouteDialog() {
        val audioManager = getSystemService(AudioManager::class.java)
        val active = getPreferredMediaOutputs(audioManager)
        val available = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            .distinctBy { it.type to it.productName.toString() }
        val routeSummary = buildString {
            append(getString(R.string.route_current_output))
            append("\n")
            append(active.joinToString { deviceLabel(it) }.ifBlank { getString(R.string.route_unknown) })
            append("\n\n")
            append(getString(R.string.route_android_managed))
            append("\n\n")
            append(getString(R.string.route_available_devices))
            append("\n")
            append(available.joinToString("\n") { "• ${deviceLabel(it)}" }.ifBlank { getString(R.string.route_no_devices) })
        }
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.route_output_title)
            .setMessage(routeSummary)
            .setNegativeButton(R.string.close, null)
            .setPositiveButton(R.string.route_android_controls) { _, _ ->
                runCatching { startActivity(Intent(Settings.Panel.ACTION_VOLUME)) }
                    .onFailure { showMessage(getString(R.string.route_panel_unavailable)) }
            }
            .show()
    }

    private fun showInputRouteDialog() {
        val inputs = getSystemService(AudioManager::class.java).getDevices(AudioManager.GET_DEVICES_INPUTS)
            .distinctBy { it.type to it.productName.toString() }
        val sourceSummary = buildString {
            append(getString(R.string.route_input_scope))
            append("\n\n")
            append(getString(R.string.route_available_devices))
            append("\n")
            append(inputs.joinToString("\n") { "• ${deviceLabel(it)}" }.ifBlank { getString(R.string.route_no_devices) })
        }
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.route_input_title)
            .setMessage(sourceSummary)
            .setPositiveButton(R.string.close, null)
            .show()
    }

    private fun showAboutDialog() {
        val version = packageManager.getPackageInfo(packageName, 0).versionName.orEmpty()
        val details = getString(R.string.about_details, version)
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.about_title)
            .setMessage(details)
            .setNegativeButton(R.string.close, null)
            .setPositiveButton(R.string.about_project) { _, _ ->
                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/wwmm/easyeffects")))
            }
            .show()
    }

    private fun showResetDialog() {
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.reset)
            .setMessage(R.string.reset_confirmation)
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.reset) { _, _ -> model.reset() }
            .show()
    }

    private fun createGraphPane(): View {
        val container = android.widget.FrameLayout(this)
        val destinations = listOf("equalizer", "bass_boost", "virtualizer", "loudness_enhancer", "compressor", "expander", "gate", "limiter")
        destinations.forEach { destination ->
            val scroll = ScrollView(this).apply {
                isFillViewport = true
                visibility = if (destination == activePlugin) View.VISIBLE else View.GONE
                setBackgroundColor(getColor(R.color.ee_background))
            }
            val content = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(16), dp(12), dp(16), dp(24))
            }
            scroll.addView(content)
            pluginScreens[destination] = scroll
            pluginContents[destination] = content
            container.addView(scroll, android.widget.FrameLayout.LayoutParams(-1, -1))
        }
        mainScroll = pluginScreens.getValue("equalizer") as ScrollView
        mainContentColumn = pluginContents.getValue("equalizer")
        graph = EqualizerGraphView(this@MainActivity).apply {
            layoutParams = LinearLayout.LayoutParams(-1, dp(280))
            setBackgroundResource(R.drawable.ee_card_background)
            contentDescription = getString(R.string.graph_help)
            connect(model, nativeEqualizer)
            onBandSelected = { refreshBandControls(it) }
        }
        mainContentColumn.addView(graph, graph.layoutParams as LinearLayout.LayoutParams)
        mainContentColumn.addView(TextView(this@MainActivity).apply {
            text = getString(R.string.graph_help)
            textSize = 12f
            setTextColor(getColor(R.color.ee_text_secondary))
            setPadding(dp(4), dp(6), dp(4), dp(10))
        })
        return container
    }

    private fun createAppPage(): View {
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(getColor(R.color.ee_background))
        }
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(24))
        }
        content.addView(sectionTitle(getString(R.string.app_page_title)))
        content.addView(TextView(this).apply {
            text = getString(R.string.app_audio_scope)
            textSize = 13f
            setTextColor(getColor(R.color.ee_text_secondary))
            setPadding(dp(2), dp(2), dp(2), dp(12))
        })
        globalStatus = TextView(this).apply {
            setTextColor(getColor(R.color.ee_text_secondary))
            textSize = 12f
            setPadding(dp(2), dp(4), dp(2), dp(14))
        }
        content.addView(globalStatus)
        content.addView(sectionTitle(getString(R.string.equalizer_bypass)))
        bypassSwitch = Switch(this).apply {
            text = getString(R.string.equalizer_bypass)
            setTextColor(getColor(R.color.ee_text))
            isChecked = model.bypass
            setOnCheckedChangeListener { _, checked -> if (!updatingControls) model.setBypass(checked) }
        }
        content.addView(bypassSwitch)
        content.addView(sectionTitle(getString(R.string.presets)))
        content.addView(TextView(this).apply {
            text = getString(R.string.desktop_preset_conversion_note)
            textSize = 12f
            setTextColor(getColor(R.color.ee_text_secondary))
            setPadding(dp(2), dp(2), dp(2), dp(8))
        })
        content.addView(textAction(getString(R.string.load_desktop_preset)) { loadPackagedDesktopPreset() })
        listOf(
            getString(R.string.import_preset) to { importPreset.launch(arrayOf("application/json", "text/plain", "application/octet-stream")) },
            getString(R.string.import_apo_graphiceq) to { importPreset.launch(arrayOf("text/plain", "application/octet-stream")) },
            getString(R.string.export_json) to { exportJsonPreset.launch("EasyEffects.json") },
            getString(R.string.export_apo) to { exportApoPreset.launch("EasyEffects.txt") },
        ).forEach { (label, action) -> content.addView(textAction(label, action)) }
        content.addView(sectionTitle(getString(R.string.actions)))
        content.addView(textAction(getString(R.string.reset)) { showResetDialog() })
        scroll.addView(content)
        return scroll
    }

    private fun createAboutPage(): View {
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(getColor(R.color.ee_background))
        }
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(24))
        }
        content.addView(android.widget.ImageView(this).apply {
            setImageResource(R.drawable.easyeffects_logo)
            contentDescription = getString(R.string.app_name)
            scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
        }, LinearLayout.LayoutParams(dp(88), dp(88)).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            topMargin = dp(8)
            bottomMargin = dp(12)
        })
        content.addView(sectionTitle(getString(R.string.about_title)))
        val version = packageManager.getPackageInfo(packageName, 0).versionName.orEmpty()
        content.addView(TextView(this).apply {
            text = getString(R.string.about_details, version)
            textSize = 14f
            setTextColor(getColor(R.color.ee_text_secondary))
            setPadding(dp(2), dp(8), dp(2), dp(18))
        })
        content.addView(textAction(getString(R.string.about_project)) {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/wwmm/easyeffects")))
        })
        content.addView(textAction(getString(R.string.about_sponsors)) {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/sponsors/wwmm")))
        })
        scroll.addView(content)
        return scroll
    }

    private fun createBottomNavigation(): View {
        bottomNavigation = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setBackgroundResource(R.drawable.ee_bottom_bar_background)
            setPadding(dp(8), dp(3), dp(8), dp(2))
        }
        val destinations = listOf(
            Triple(MainSection.PLUGINS, R.drawable.ic_nav_plugins, R.string.plugins),
            Triple(MainSection.APP, R.drawable.ic_nav_app, R.string.app_page_title),
            Triple(MainSection.ABOUT, R.drawable.ic_nav_about, R.string.about_nav),
        )
        destinations.forEach { (section, icon, label) ->
            val item = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER
                isClickable = true
                isFocusable = true
                setPadding(dp(8), dp(2), dp(8), dp(2))
                contentDescription = getString(label)
                setOnClickListener { showSection(section) }
            }
            val indicator = View(this).apply {
                setBackgroundColor(getColor(R.color.ee_accent))
                visibility = View.INVISIBLE
            }
            item.addView(indicator, LinearLayout.LayoutParams(dp(28), dp(2)).apply { bottomMargin = dp(3) })
            item.addView(android.widget.ImageView(this).apply {
                setImageResource(icon)
                scaleType = android.widget.ImageView.ScaleType.CENTER_INSIDE
            }, LinearLayout.LayoutParams(dp(22), dp(22)))
            item.addView(TextView(this).apply {
                text = getString(label)
                textSize = 10f
                gravity = Gravity.CENTER
                setPadding(0, dp(2), 0, 0)
            })
            bottomNavigation.addView(item, LinearLayout.LayoutParams(0, -1, 1f))
            bottomNavigationItems[section] = item
        }
        return bottomNavigation
    }

    private fun showSection(section: MainSection) {
        currentSection = section
        if (::drawerLayout.isInitialized && drawerLayout.isDrawerOpen(GravityCompat.START)) {
            drawerLayout.closeDrawer(GravityCompat.START)
        }
        if (::pluginPage.isInitialized) pluginPage.visibility = if (section == MainSection.PLUGINS) View.VISIBLE else View.GONE
        if (::appPage.isInitialized) appPage.visibility = if (section == MainSection.APP) View.VISIBLE else View.GONE
        if (::aboutPage.isInitialized) aboutPage.visibility = if (section == MainSection.ABOUT) View.VISIBLE else View.GONE
        if (::pluginHeader.isInitialized) {
            pluginHeader.visibility = if (section == MainSection.PLUGINS) View.VISIBLE else View.GONE
        }
        if (::drawerIcon.isInitialized) {
            drawerIcon.visibility = if (section == MainSection.PLUGINS) View.VISIBLE else View.INVISIBLE
            drawerLayout.setDrawerLockMode(
                if (section == MainSection.PLUGINS) DrawerLayout.LOCK_MODE_UNLOCKED else DrawerLayout.LOCK_MODE_LOCKED_CLOSED,
                GravityCompat.START,
            )
        }
        bottomNavigationItems.forEach { (destination, item) ->
            val selected = destination == section
            item.isSelected = selected
            item.contentDescription = getString(when (destination) {
                MainSection.PLUGINS -> R.string.plugins
                MainSection.APP -> R.string.app_page_title
                MainSection.ABOUT -> R.string.about_nav
            })
            (item.getChildAt(0) as View).visibility = if (selected) View.VISIBLE else View.INVISIBLE
            (item.getChildAt(1) as android.widget.ImageView).imageTintList = android.content.res.ColorStateList.valueOf(
                getColor(if (selected) R.color.ee_accent else R.color.ee_text_secondary),
            )
            (item.getChildAt(2) as TextView).setTextColor(getColor(if (selected) R.color.ee_accent else R.color.ee_text_secondary))
        }
    }

    private fun createSidebar(): View {
        val sidebar = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(getColor(R.color.ee_background))
        }
        val pluginList = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(20))
        }
        sidebar.addView(pluginList)
        var panel = pluginContents.getValue("equalizer")
        val pluginNavigationItems = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        pluginList.addView(pluginNavigationItems)
        var selectedPluginAction: TextView? = null
        val pluginDestinations = listOf(
            getString(R.string.equalizer_title) to "equalizer",
            getString(R.string.android_effect_bass_boost) to "bass_boost",
            getString(R.string.android_effect_virtualizer) to "virtualizer",
            getString(R.string.android_effect_loudness) to "loudness_enhancer",
            getString(R.string.android_dynamics_compressor) to "compressor",
            getString(R.string.android_dynamics_expander) to "expander",
            getString(R.string.android_dynamics_gate) to "gate",
            getString(R.string.android_dynamics_limiter) to "limiter",
        )
        pluginDestinations.forEach { (label, destination) ->
            lateinit var action: TextView
            action = textAction(label) {
                selectedPluginAction?.let {
                    it.isSelected = false
                    it.setTextColor(getColor(R.color.ee_text_secondary))
                }
                selectedPluginAction = action
                action.isSelected = true
                action.setTextColor(getColor(R.color.ee_text))
                drawerLayout.closeDrawer(GravityCompat.START)
                showPlugin(destination)
            }
            action.textSize = 12f
            action.maxLines = 1
            action.minHeight = dp(48)
            action.setPadding(dp(10), dp(6), dp(10), dp(6))
            action.setBackgroundResource(R.drawable.ee_sidebar_item_background)
            action.layoutParams = LinearLayout.LayoutParams(-1, -2).apply {
                topMargin = dp(4)
                bottomMargin = dp(4)
            }
            pluginNavigationItems.addView(action)
            if (destination == "equalizer") {
                selectedPluginAction = action
                action.isSelected = true
                action.setTextColor(getColor(R.color.ee_text))
            }
        }
        panel.addView(sectionTitle(getString(R.string.equalizer_title)))
        globalSwitch = Switch(this).apply {
            text = getString(R.string.enabled)
            setTextColor(getColor(R.color.ee_text))
            isChecked = AudioRuntime.globalOutputEqualizer.isEnabled || AudioRuntime.globalOutputEqualizer.isRequested
            contentDescription = getString(R.string.equalizer_title)
            setOnCheckedChangeListener { _, checked -> if (!refreshingGlobalSwitch) setGlobalEqualizerEnabled(checked) }
        }
        panel.addView(globalSwitch)
        panel.addView(sectionTitle(getString(R.string.preamp)))
        preampText = valueLabel()
        panel.addView(preampText)
        panel.addView(SeekBar(this).apply { max = 480; progress = ((model.preampDb + 24f) * 10).toInt(); setOnSeekBarChangeListener(seekListener { model.setPreamp(it / 10f - 24f) }) })
        panel.addView(sectionTitle(getString(R.string.bands)))
        bandCountText = valueLabel()
        panel.addView(bandCountText)
        panel.addView(SeekBar(this).apply { max = 31; progress = model.bandCount - 1; setOnSeekBarChangeListener(seekListener { model.setBandCount(it + 1); refreshBandSpinner() }) })
        editBandAction = textAction(getString(R.string.edit_band)) { showBandEditor() }
        panel.addView(editBandAction)
        panel.addView(sectionTitle(getString(R.string.controls)))
        listOf(
            getString(R.string.flat_response) to { model.flatten() },
            getString(R.string.sort_bands) to { model.sortBandsByFrequency() },
            getString(R.string.calculate_frequencies) to { model.distributeFrequenciesLogarithmically() },
            getString(R.string.reset) to { model.reset() },
        ).forEach { (label, action) -> panel.addView(textAction(label, action)) }
        AndroidPlatformEffect.entries.forEach { effect ->
            val destination = when (effect) {
                AndroidPlatformEffect.BASS_BOOST -> "bass_boost"
                AndroidPlatformEffect.VIRTUALIZER -> "virtualizer"
                AndroidPlatformEffect.LOUDNESS_ENHANCER -> "loudness_enhancer"
            }
            panel = pluginContents.getValue(destination)
            panel.addView(sectionTitle(getString(effect.label)))
            val state = AudioRuntime.platformEffects.state(effect)
            val effectSwitch = Switch(this).apply {
                text = getString(R.string.enabled)
                setTextColor(getColor(R.color.ee_text))
                isChecked = state.enabled
                contentDescription = getString(effect.label)
                setOnCheckedChangeListener { _, checked ->
                    if (updatingControls) return@setOnCheckedChangeListener
                    if (processingTarget != ProcessingTarget.OUTPUT) {
                        isChecked = false
                        return@setOnCheckedChangeListener
                    }
                    runCatching {
                        AudioRuntime.platformEffects.set(effect, enabled = checked)
                        if (checked) GlobalEqualizerService.start(this@MainActivity)
                        stopOutputServiceIfIdle()
                    }.onFailure {
                        runCatching { AudioRuntime.platformEffects.set(effect, enabled = false) }
                        updatingControls = true
                        isChecked = false
                        updatingControls = false
                        stopOutputServiceIfIdle()
                        showMessage(it.localizedMessage ?: it.javaClass.simpleName)
                    }
                }
            }
            platformEffectSwitches[effect] = effectSwitch
            panel.addView(effectSwitch)
            panel.addView(TextView(this).apply {
                text = getString(R.string.android_system_effects_note)
                textSize = 12f
                setTextColor(getColor(R.color.ee_text_secondary))
                setPadding(dp(2), dp(2), dp(2), dp(8))
            })
            val valueLabel = valueLabel()
            val range = effect.maximum - effect.minimum
            val seek = SeekBar(this).apply {
                max = range
                progress = state.value - effect.minimum
                setOnSeekBarChangeListener(seekListener { progress ->
                    val value = effect.minimum + progress
                    valueLabel.text = platformEffectValue(effect, value)
                    runCatching { AudioRuntime.platformEffects.set(effect, value = value) }
                        .onFailure { showMessage(it.localizedMessage ?: it.javaClass.simpleName) }
                })
            }
            valueLabel.text = platformEffectValue(effect, state.value)
            panel.addView(valueLabel)
            panel.addView(seek)
        }
        AndroidDynamicsStage.entries.forEach { stage ->
            val destination = when (stage) {
                AndroidDynamicsStage.COMPRESSOR -> "compressor"
                AndroidDynamicsStage.EXPANDER -> "expander"
                AndroidDynamicsStage.GATE -> "gate"
                AndroidDynamicsStage.LIMITER -> "limiter"
            }
            panel = pluginContents.getValue(destination)
            panel.addView(sectionTitle(getString(stage.label)))
            val state = AudioRuntime.dynamicsProcessing.state(stage)
            val stageSwitch = Switch(this).apply {
                text = getString(R.string.enabled)
                setTextColor(getColor(R.color.ee_text))
                isChecked = state.enabled
                contentDescription = getString(stage.label)
                setOnCheckedChangeListener { _, checked ->
                    if (updatingControls) return@setOnCheckedChangeListener
                    if (processingTarget != ProcessingTarget.OUTPUT) {
                        isChecked = false
                        return@setOnCheckedChangeListener
                    }
                    runCatching {
                        AudioRuntime.dynamicsProcessing.set(stage, enabled = checked)
                        if (checked) GlobalEqualizerService.start(this@MainActivity)
                    }.onFailure {
                        runCatching { AudioRuntime.dynamicsProcessing.set(stage, enabled = false) }
                        updatingControls = true
                        isChecked = false
                        updatingControls = false
                        stopOutputServiceIfIdle()
                        showMessage(it.localizedMessage ?: it.javaClass.simpleName)
                    }
                    if (!checked) stopOutputServiceIfIdle()
                }
            }
            dynamicsStageSwitches[stage] = stageSwitch
            panel.addView(stageSwitch)
            panel.addView(TextView(this).apply {
                text = getString(R.string.android_dynamics_note)
                textSize = 12f
                setTextColor(getColor(R.color.ee_text_secondary))
                setPadding(dp(2), dp(2), dp(2), dp(8))
            })
            val thresholdLabel = valueLabel().apply { text = getString(R.string.android_threshold_value, state.thresholdDb) }
            panel.addView(thresholdLabel)
            panel.addView(SeekBar(this).apply {
                max = 600
                progress = ((state.thresholdDb + 60f) * 10f).toInt()
                setOnSeekBarChangeListener(seekListener { progress ->
                    val threshold = -60f + progress / 10f
                    thresholdLabel.text = getString(R.string.android_threshold_value, threshold)
                    runCatching { AudioRuntime.dynamicsProcessing.set(stage, thresholdDb = threshold) }
                        .onFailure { showMessage(it.localizedMessage ?: it.javaClass.simpleName) }
                })
            })
            if (stage == AndroidDynamicsStage.COMPRESSOR || stage == AndroidDynamicsStage.EXPANDER) {
                val ratioLabel = valueLabel().apply { text = getString(R.string.android_ratio_value, state.ratio) }
                panel.addView(ratioLabel)
                panel.addView(SeekBar(this).apply {
                    max = 190
                    progress = ((state.ratio - 1f) * 10f).toInt()
                    setOnSeekBarChangeListener(seekListener { progress ->
                        val ratio = 1f + progress / 10f
                        ratioLabel.text = getString(R.string.android_ratio_value, ratio)
                        runCatching { AudioRuntime.dynamicsProcessing.set(stage, ratio = ratio) }
                            .onFailure { showMessage(it.localizedMessage ?: it.javaClass.simpleName) }
                    })
                })
            }
        }
        return sidebar
    }

    private fun showPlugin(destination: String) {
        if (destination !in pluginScreens) return
        activePlugin = destination
        pluginScreens.forEach { (id, screen) -> screen.visibility = if (id == destination) View.VISIBLE else View.GONE }
        (pluginScreens[destination] as? ScrollView)?.scrollTo(0, 0)
    }

    private fun showBandEditor() {
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(8), dp(20), dp(12))
        }
        selectedBandSpinner = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                (0 until model.bandCount).map { getString(R.string.band_number, it + 1) })
            setSelection(graph.selectedBand, false)
            onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                    graph.selectBand(position)
                    refreshBandControls(position)
                }
            }
        }
        content.addView(sectionTitle(getString(R.string.band)))
        content.addView(selectedBandSpinner)
        bandStatus = valueLabel()
        content.addView(bandStatus)
        val currentBand = { model.bands[graph.selectedBand.coerceIn(0, model.bandCount - 1)] }
        typeSpinner = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                FilterType.entries.map { getString(it.labelResource) })
            setSelection(currentBand().type.ordinal, false)
            onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                    if (!updatingControls) model.setBand(graph.selectedBand, currentBand().copy(type = FilterType.entries[position]))
                }
            }
        }
        content.addView(typeSpinner)
        channelSpinner = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                listOf(getString(R.string.channel_stereo), getString(R.string.channel_left), getString(R.string.channel_right)))
            setSelection(currentBand().channelMode, false)
            onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                    if (!updatingControls) model.setBand(graph.selectedBand, currentBand().copy(channelMode = position))
                }
            }
        }
        content.addView(channelSpinner)
        content.addView(sectionTitle(getString(R.string.quality)))
        qualityText = valueLabel()
        content.addView(qualityText)
        qualitySeek = SeekBar(this).apply {
            max = 1790
            setOnSeekBarChangeListener(seekListener { progress ->
                model.setBand(graph.selectedBand, currentBand().copy(q = 0.1f + progress / 100f))
            })
        }
        content.addView(qualitySeek)
        enabledSwitch = Switch(this).apply {
            text = getString(R.string.enabled)
            setTextColor(getColor(R.color.ee_text))
            setOnCheckedChangeListener { _, checked ->
                if (!updatingControls) model.setBand(graph.selectedBand, currentBand().copy(enabled = checked))
            }
        }
        content.addView(enabledSwitch)
        soloSwitch = Switch(this).apply {
            text = getString(R.string.solo)
            setTextColor(getColor(R.color.ee_text))
            setOnCheckedChangeListener { _, checked ->
                if (!updatingControls) model.setBand(graph.selectedBand, currentBand().copy(solo = checked))
            }
        }
        content.addView(soloSwitch)
        refreshBandControls(graph.selectedBand)
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.equalizer_band_editor_title)
            .setView(ScrollView(this).apply { addView(content) })
            .setPositiveButton(R.string.close, null)
            .show()
    }

    private fun refreshBandSpinner() {
        if (!::selectedBandSpinner.isInitialized) return
        val labels = (0 until model.bandCount).map { getString(R.string.band_number, it + 1) }
        selectedBandSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        selectedBandSpinner.setSelection(graph.selectedBand.coerceIn(0, model.bandCount - 1), false)
        selectedBandSpinner.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) { graph.selectBand(position); refreshBandControls(position) }
        }
    }

    private fun refreshBandControls(index: Int) {
        if (model.bandCount == 0) return
        val i = index.coerceIn(0, model.bandCount - 1); val band = model.bands[i]
        val statusText = getString(R.string.band_status, i + 1, band.frequencyHz, band.gainDb, band.q)
        if (::editBandAction.isInitialized) editBandAction.text = statusText
        if (!::bandStatus.isInitialized) return
        bandStatus.text = statusText
        updatingControls = true
        if (::selectedBandSpinner.isInitialized && selectedBandSpinner.selectedItemPosition != i) selectedBandSpinner.setSelection(i, false)
        if (::qualityText.isInitialized) {
            qualityText.text = getString(R.string.quality_value, band.q); qualitySeek.progress = ((band.q - 0.1f) * 100).toInt().coerceIn(0, 1790)
            enabledSwitch.isChecked = band.enabled; soloSwitch.isChecked = band.solo
            typeSpinner.setSelection(FilterType.entries.indexOf(band.type), false)
            channelSpinner.setSelection(band.channelMode, false)
        }
        updatingControls = false
    }

    private fun setGlobalEqualizerEnabled(enabled: Boolean) {
        if (processingTarget != ProcessingTarget.OUTPUT) {
            refreshGlobalStatus()
            return
        }
        if (enabled) {
            runCatching { AudioRuntime.globalOutputEqualizer.enable() }
                .onFailure { failure -> AudioRuntime.globalOutputEqualizer.recordFailure(failure.localizedMessage ?: failure.javaClass.simpleName) }
            runCatching { GlobalEqualizerService.start(this) }
                .onFailure { failure -> AudioRuntime.globalOutputEqualizer.recordFailure(failure.localizedMessage ?: failure.javaClass.simpleName) }
            refreshGlobalStatus()
        }
        else {
            AudioRuntime.globalOutputEqualizer.disable()
            stopOutputServiceIfIdle()
        }
        refreshGlobalStatus()
    }

    private fun stopOutputServiceIfIdle() {
        if (!AudioRuntime.hasEnabledOutputPlugins()) GlobalEqualizerService.stop(this)
    }

    private fun refreshLabels() {
        if (!::bandCountText.isInitialized) return
        preampText.text = getString(R.string.preamp_value, model.preampDb)
        if (::bypassSwitch.isInitialized && bypassSwitch.isChecked != model.bypass) {
            updatingControls = true
            bypassSwitch.isChecked = model.bypass
            updatingControls = false
        }
        bandCountText.text = getString(R.string.band_count_value, model.bandCount, 32)
        refreshGlobalStatus(); refreshBandSpinner(); refreshBandControls(graph.selectedBand)
    }

    private fun refreshGlobalStatus() {
        if (!::globalStatus.isInitialized) return
        if (processingTarget == ProcessingTarget.INPUT) {
            if (AudioRuntime.globalOutputEqualizer.isRequested || AudioRuntime.globalOutputEqualizer.isEnabled) {
                disableOutputProcessing()
            }
            if (::globalSwitch.isInitialized) {
                refreshingGlobalSwitch = true
                globalSwitch.isChecked = false
                refreshingGlobalSwitch = false
            }
            globalStatus.setText(R.string.route_input_scope)
            globalStatus.visibility = View.VISIBLE
            refreshProcessingTargetControls()
            return
        }
        refreshProcessingTargetControls()
        val requested = AudioRuntime.globalOutputEqualizer.isRequested; val active = AudioRuntime.globalOutputEqualizer.isEnabled
        if (::globalSwitch.isInitialized) {
            refreshingGlobalSwitch = true
            globalSwitch.isChecked = requested || active
            refreshingGlobalSwitch = false
        }
        val error = AudioRuntime.globalOutputEqualizer.lastError
        globalStatus.text = when {
            error != null && AudioRuntime.globalOutputEqualizer.activeSessionCount > 0 -> getString(R.string.global_eq_sessions_active, AudioRuntime.globalOutputEqualizer.activeSessionCount)
            error != null -> getString(R.string.global_eq_backend_error, error)
            requested && !active -> getString(R.string.global_eq_connecting)
            active -> getString(R.string.global_eq_scope_limited)
            else -> ""
        }
        globalStatus.visibility = if (requested || error != null) View.VISIBLE else View.GONE
    }

    private fun importPreset(uri: Uri) {
        runCatching { val text = contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() } ?: throw IOException(getString(R.string.preset_read_failed)); if (text.trimStart().startsWith("{")) model.loadJson(JSONObject(text)) else model.loadEqualizerApo(text) }
            .onSuccess { showMessage(getString(R.string.preset_imported)) }.onFailure { showMessage(it.message ?: getString(R.string.preset_import_failed)) }
    }

    private fun loadPackagedDesktopPreset() {
        runCatching {
            val preset = assets.open("presets/OS-Subwoofer-Android.json")
                .bufferedReader().use { JSONObject(it.readText()) }
            model.loadJson(preset)
        }.onSuccess {
            showMessage(getString(R.string.desktop_preset_loaded))
            showSection(MainSection.PLUGINS)
            showPlugin("equalizer")
        }.onFailure {
            showMessage(getString(R.string.preset_import_failed) + ": " + (it.localizedMessage ?: it.javaClass.simpleName))
        }
    }
    private fun writePreset(uri: Uri, contents: String) { runCatching { contentResolver.openOutputStream(uri)?.bufferedWriter()?.use { it.write(contents) } ?: throw IOException(getString(R.string.preset_write_failed)) }.onSuccess { showMessage(getString(R.string.preset_exported)) }.onFailure { showMessage(it.message ?: getString(R.string.preset_export_failed)) } }
    private fun seekListener(onProgress: (Int) -> Unit) = object : SeekBar.OnSeekBarChangeListener { override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) { if (fromUser && !updatingControls) onProgress(progress) }; override fun onStartTrackingTouch(seekBar: SeekBar) = Unit; override fun onStopTrackingTouch(seekBar: SeekBar) = Unit }
    private fun platformEffectValue(effect: AndroidPlatformEffect, value: Int): String = when (effect) {
        AndroidPlatformEffect.BASS_BOOST, AndroidPlatformEffect.VIRTUALIZER -> "$value / 1000"
        AndroidPlatformEffect.LOUDNESS_ENHANCER -> String.format(java.util.Locale.getDefault(), "+%.1f dB", value / 100f)
    }
    private fun sectionTitle(text: String) = TextView(this).apply { this.text = text; textSize = 11f; letterSpacing = 0.07f; setTypeface(typeface, android.graphics.Typeface.BOLD); setTextColor(getColor(R.color.ee_text_secondary)); setPadding(dp(2), dp(16), 0, dp(5)) }
    private fun valueLabel() = TextView(this).apply { textSize = 14f; setTextColor(getColor(R.color.ee_text_secondary)); setPadding(dp(2), dp(3), dp(2), dp(2)) }
    private fun textAction(label: String, action: () -> Unit) = TextView(this).apply { text = label; gravity = Gravity.CENTER_VERTICAL; minHeight = dp(52); textSize = 14f; setTextColor(getColor(R.color.ee_text)); isClickable = true; isFocusable = true; setPadding(dp(16), 0, dp(16), 0); setBackgroundResource(R.drawable.ee_action_background); layoutParams = LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(5); bottomMargin = dp(5) }; setOnClickListener { action() } }
    private fun horizontalRow() = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
    private fun updateDrawerIcon(open: Boolean) {
        drawerIcon.setImageResource(if (open) R.drawable.ic_plugins_close else R.drawable.ic_plugins_menu)
        drawerIcon.contentDescription = getString(if (open) R.string.close_plugins else R.string.open_plugins)
    }
    private fun showMessage(message: String) = Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
}

private fun Float.powCompat(exponent: Float): Float = kotlin.math.exp(kotlin.math.ln(this) * exponent)

private enum class MainSection { PLUGINS, APP, ABOUT }
private enum class ProcessingTarget { OUTPUT, INPUT }
