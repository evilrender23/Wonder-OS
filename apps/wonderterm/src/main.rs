mod app;
mod boxdraw;
mod config;
mod cursor;
mod cut;
mod fonts;
mod grid;
mod input;
mod session;
mod theme;

use std::path::PathBuf;

use clap::Parser;
use eframe::egui;

/// WonderlandWM's terminal: animated cursor, tabs, Hermes palette.
#[derive(Parser)]
#[command(version)]
struct Cli {
    /// Run this command instead of the shell; everything after it is its arguments.
    #[arg(short = 'e', long = "command", num_args = 1.., allow_hyphen_values = true)]
    command: Vec<String>,

    /// Start in this directory.
    #[arg(short = 'd', long, value_name = "DIR")]
    working_directory: Option<PathBuf>,

    /// Read this file instead of ~/.config/wonderterm/config.toml.
    #[arg(short, long, value_name = "FILE")]
    config: Option<PathBuf>,

    /// Print the default configuration and exit.
    #[arg(long)]
    print_default_config: bool,

    /// Save the window to a PNG after --screenshot-delay and exit.
    #[arg(long, value_name = "PNG")]
    screenshot: Option<PathBuf>,

    #[arg(long, value_name = "MS", default_value_t = 1200)]
    screenshot_delay: u64,
}

fn main() -> eframe::Result {
    let cli = Cli::parse();
    if cli.print_default_config {
        print!("{}", config::DEFAULT_CONFIG);
        return Ok(());
    }

    let path = cli.config.unwrap_or_else(config::default_path);
    let (cfg, error) = match config::load(&path) {
        Ok(cfg) => (cfg, None),
        Err(e) => {
            eprintln!("wonderterm: {e}\nwonderterm: using the defaults");
            (config::Config::default(), Some(e))
        }
    };

    let fonts = fonts::definitions(&cfg.font.family);
    let size = app::window_size(&fonts, &cfg);
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_app_id("wonderterm")
            .with_title("wonderterm")
            .with_inner_size(size)
            .with_min_inner_size([320.0, 160.0])
            .with_decorations(false)
            .with_transparent(true),
        ..Default::default()
    };
    let launch = session::Launch {
        command: cli.command,
        cwd: cli.working_directory,
    };
    let shot = cli
        .screenshot
        .map(|path| (path, cli.screenshot_delay as f64 / 1000.0));

    eframe::run_native(
        "wonderterm",
        options,
        Box::new(move |cc| Ok(Box::new(app::App::new(cc, cfg, error, fonts, launch, shot)))),
    )
}
