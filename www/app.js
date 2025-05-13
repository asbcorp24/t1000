// app.js — логика веб-интерфейса для взаимодействия с ESP32

document.addEventListener('DOMContentLoaded', () => {
  // Ссылки на элементы страницы
  const fileListEl = document.getElementById('fileList');
  const selRefEl   = document.getElementById('selRef');
  const btnRef     = document.getElementById('btnRef');
  const btnTest    = document.getElementById('btnTest');
  const uploadFile = document.getElementById('uploadFile');

  // Функция подгрузки списка эталонов из /api/list
  function loadRefs() {
    fetch('/api/list')
      .then(res => res.json())
      .then(list => {
        // Очистить текущие списки
        fileListEl.innerHTML = '';
        selRefEl.innerHTML   = '';

        list.forEach(item => {
          // Заполняем вкладку «Список»
          const li = document.createElement('li');
          li.className = 'list-group-item d-flex justify-content-between align-items-center';
          li.textContent = item.file;

          // Кнопка «Скачать JSON»
          const dl = document.createElement('a');
          dl.href        = `/api/download?file=${encodeURIComponent(item.file)}`;
          dl.download    = `${item.file}.json`;
          dl.className   = 'btn btn-sm btn-outline-primary';
          dl.textContent = 'JSON';
          li.appendChild(dl);
          fileListEl.appendChild(li);

          // Заполняем выпадающий список для теста
          const opt = document.createElement('option');
          opt.value = item.file;
          opt.text  = item.file;
          selRefEl.appendChild(opt);
        });
      })
      .catch(err => {
        console.error('Ошибка при загрузке списка эталонов:', err);
        alert('Не удалось получить список эталонов');
      });
  }

  // Инициалная загрузка списка
  loadRefs();

  // Обработчик кнопки «Получить эталон»
  btnRef.addEventListener('click', () => {
    const name = prompt('Введите имя для нового эталона:');
    if (!name) return;

    // Отправляем POST /api/reference?name=...
    fetch('/api/reference', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ name })
    })
    .then(res => {
      if (res.ok) {
        alert('Эталон успешно создан');
        loadRefs();
      } else {
        alert('Ошибка при создании эталона');
      }
    })
    .catch(err => {
      console.error('Ошибка при запросе /api/reference:', err);
      alert('Сетевая ошибка');
    });
  });

  // Обработчик загрузки файла эталона (.bin) на SD (вкладка «Список»)
  uploadFile.addEventListener('change', () => {
    if (!uploadFile.files.length) return;
    const file = uploadFile.files[0];
    const form = new FormData();
    form.append('file', file, file.name);

    fetch('/api/upload', {
      method: 'POST',
      body: form
    })
    .then(res => {
      if (res.ok) {
        alert('Файл загружен');
        loadRefs();
      } else {
        alert('Ошибка загрузки файла');
      }
    })
    .catch(err => {
      console.error('Ошибка при запросе /api/upload:', err);
      alert('Сетевая ошибка');
    });
  });

  // Обработчик кнопки «Запустить тест»
  btnTest.addEventListener('click', () => {
    const file = selRefEl.value;
    if (!file) {
      alert('Выберите эталон');
      return;
    }

    // Отправляем POST /api/test?file=...
    fetch('/api/test', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ file })
    })
    .then(res => res.json())
    .then(json => {
      if (json.status === 'ok') {
        alert('Тест завершён. Смотрите результаты на дисплее');
      } else {
        alert('Ошибка при выполнении теста');
      }
    })
    .catch(err => {
      console.error('Ошибка при запросе /api/test:', err);
      alert('Сетевая ошибка');
    });
  });

  // Перезагружаем список при переключении на вкладку «Список»
  const tabListBtn = document.querySelector('[data-bs-target="#tab-list"]');
  if (tabListBtn) {
    tabListBtn.addEventListener('shown.bs.tab', loadRefs);
  }
});
